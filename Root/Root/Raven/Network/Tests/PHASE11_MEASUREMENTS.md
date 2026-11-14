# Phase 11 制御通信の計測基準（2026-11-14）

Windows x64 / Intel64 Family 6 Model 165 Stepping 5 / 論理Processor 20。
MSVC 19.51.36260 / C++17 / O2 / NDEBUG。専用exeでCPU処理と127.0.0.1のTCP往復を測定した。
Runtime、Wire形式、Queue上限は変更していない。実ゲームの同期性能や外部回線の測定ではない。

## 再実行と保存物

リポジトリルートから順番に実行する。同じmodeの並行実行はしないこと。

```powershell
./scripts/measure-windows-network.ps1
./scripts/measure-windows-network.ps1 -MeasureMemory
./scripts/test-windows-structured-data.ps1
```

localhostへの接続が必要。sandboxではConnectが10013で拒否されたためsandbox外で実行した。
外部Interfaceでは待ち受けない。接続・検証・期限の失敗は非ゼロ終了し、Skipして成功扱いにしない。
失敗時は`results.pending.csv`を残し、成功結果へ昇格させない。過去の`results.csv`は今回の成功を意味しない。

生成先は`artifacts/network-benchmarks/time/`と`memory/`の`results.csv`、`metadata.json`、`traffic-estimates.csv`。
通常アプリと回帰テストにはmainやAllocation追跡を追加しない。時間計測はglobal newを置換せず、
メモリ専用ビルドだけPhase 8の`DataBenchmarkMemory`を再利用する。

今回の結果を同所の`Phase11Time.csv`、`Phase11Memory.csv`、`Phase11Traffic.csv`に保存した。
`Phase11TimeMetadata.json` / `Phase11MemoryMetadata.json`には環境・Flags・基準Commit・BenchmarkとScriptのHashを記録。
基準Commitは実装前のHEADであり、計測コード自体は同じPRの差分に含まれる。Hashは計測時の生Byte列が対象。

## 対象と区間

- FixtureはPingのみ、Ping / SetReady交互のmixed。1 / 16 / 64件を1 Batchとする。
  RequestIdは100000から、Ping nonceはUInt64最大値から減算、Readyはtrue / falseを交互に含める。
  mixedの1件ケースはPingだけであり、pingの1件ケースと同じ入力になる。
- 各区間の前に5 Batchをwarm-up。時間は101 Sampleの中央値とnearest-rank p95。
  CSVの`samples_us`に計測順の全Sampleをセミコロン区切りで残す。時間単位は1 Batchあたりのmicrosecond。
- メモリは別ビルドでwarm-up後の1 Batch。新しく発生した全ThreadのC++要求Allocation件数・累積Bytes・
  区間内Peakを記録する。malloc、OS / Winsock / Kernel Buffer、Stack、追跡Header、
  測定開始前の保持領域、プロセス全体のRSSは含まない。既存領域の解放は集計しない。
- 入力生成、結果vectorの確保、Receiver生成、出力の入れ物の確保、CSV出力は区間外。
  CPU Sampleの出力・Receiverの破棄も区間外。Decode内部の一時Documentの生成・破棄は区間内。
  Stageごとに準備・Cache状態が違うため、時間やPeakを単純加算・減算して部品コストとはみなさない。

| stage | 区間に含む処理 | 検証 |
| --- | --- | --- |
| encode | Codec::Encode、Writerの一時確保、出力Stringの確定。Prefixは付けない。 | 区間後に全Byte列をFixtureと比較。 |
| decode | 所有DocumentへのParse、Envelope・型・値検証、所有型へのCommit。 | 区間後に全件のID・型・値・順序を比較。 |
| receive_queue | 結合FrameのFraming、Decode、Queue投入。Receiverは新規生成済み。 | 区間後に全件を取り出し、値・順序・残Queueを確認。 |
| queue_dispatch | 区間外で投入済みのQueueからTryPop、Policy確認、Handlerで値・順序を照合。 | 区間内の照合成功と残Queueを確認。 |
| tcp_round_trip | 事前Encode済みBatchを送信→Workerで受信・検証・再Encode→まとめて応答→呼び出し側で受信・Dispatch・照合。 | 両端で全件検証し、WorkerのSample内処理完了も待つ。 |

TCPは各Fixture内で接続と受信Bufferを再利用する。Socket生成・Connect / Accept・Thread起動・
初期Buffer確保・joinは区間外。WorkerはSample内の一時値を解放してから完了を通知し、
メモリ測定終了と競合させない。受信側はFrame数を数え、recvの回数をMessage数として扱わない。
エラーでもClient Close→Worker joinの順で片付ける。待機は5秒の絶対期限で制限する。

TCPの時間はOS、Scheduling、両端のDecode、応答Encode、検証、Worker完了待ちを含む。
純粋なKernel RTT、1 Messageの片道Latency、帯域上限ではない。TCP_NODELAY等の設定変更もしていない。
CPU affinity固定、CPU周波数固定、負荷隔離は行わず、Stageを固定順に実行した。時計の粒度と実行順の影響もある。
1件のping / mixedの差は入力差ではなく、実行条件の揺らぎである。CIの時間閾値として使わない。

## 結果と判断

mixed 64件の代表値（1 Batch、時間はAllocation追跡なし）:

| 区間 | 中央値 µs | p95 µs | Allocation件数 | 累積要求Byte | 区間内Peak Byte |
| --- | ---: | ---: | ---: | ---: | ---: |
| Encode | 70.4 | 88.4 | 736 | 104,896 | 5,433 |
| Decode | 86.1 | 87.2 | 1,152 | 163,328 | 992 |
| 受信・Queue投入 | 90.9 | 97.4 | 1,222 | 167,440 | 3,632 |
| 投入済みQueueのDispatch | 2.1 | 2.2 | 0 | 0 | 0 |
| TCP往復 | 293.6 | 354.7 | 3,104 | 436,672 | 992 |

TCPの小さいPeakは、warm-upで保持したBufferとSocket等を含まない区間内Peakである。
新規Receiverで出力を保持するCPU Encodeと直接比較して、総メモリ削減を示す値ではない。

mixed 64件はPayload 4,224 Byte、Prefix込み4,480 Byte、Echo往復8,960 Byte。
現行FixtureではPingはPayload 70 / Frame 74 Byte、SetReadyはPayload 62 / Frame 66 Byte。
RequestIdやnonceの符号化幅が変われば長さも変わる。

`Phase11Traffic.csv`は64件Fixtureの平均長から30 / 60 / 120 / 240 Message/sを換算した**見積もり**。
Rateを変えて実測したものではなく、Batch/sでもない。例えばmixed 120 Message/sは片道8,400 Byte/s、
同じ内容をEchoすると合計16,800 Byte/s。TCP/IP Header、ACK、再送、暗号化、接続確立は含まない。

この試験制御通信だけを理由に圧縮・Delta・QuantizationやWire変更は行わない。
投入済みQueueのDispatchは今回のBatchで確保0件だった一方、Encode / Decode区間には多数の一時確保が残る。
実用途で予算超過が判明した場合は、まずこれらの確保・入力保持を調査する。
実ゲームの同期Payloadや接続数・Tick頻度は未定のため、高頻度同期全体の最適化要否は判断しない。

時間30ケース×101 Sample、メモリ30ケース×1 Sampleはすべて件数・FIFO・値の照合成功、MSVC警告0。
既存のCPU/実Socket回帰6,125チェックも全成功、警告0。アプリDebug x64はPDB生成無効で成功し、
既存LNK4098が1件。通常PDBあり・GUIは今回検証していない。
高頻度同期のロードマップ項目は未完のまま維持し、この成果は制御通信の計測基準とする。
外部回線、実ゲーム負荷、接続数全体の予算、持続負荷でのbackpressure、確保失敗注入、
Sanitizer / ThreadSanitizer、低頻度の外れ値を評価する長時間実験は未検証。

前CommitのCIではStructured Data Jobが成功し、他4 Jobは`GLFW/glfw3native.h`欠落で失敗した。
この計測マイルストーンでは通信RuntimeやGLFW依存を変更していない。CI全体成功とは扱わない。
