# Phase 11 — 制御メッセージCodecとLoopback

この実装は通信層が存在しない段階のCPU試験導入。Socket、接続確立、配送保証、暗号化、
認証、Replay対策は提供しない。Data層へNetwork / Socket依存は追加しない。
PingとSetReadyは試験Protocolの許可リストであり、実運用Lobbyや汎用RPC機能ではない。

## Wire契約

MessagePack Mapは次の4フィールドだけを持つ。各payloadも下記の1フィールドだけを許可する。
未知・欠落・重複キー、不正UTF-8、Extension、不正型は拒否する。

| フィールド | 契約 |
| --- | --- |
| protocolVersion | 整数1のみ。Version変更は旧受信側で拒否する。 |
| messageType | 整数1 = Ping、整数2 = SetReady。それ以外は拒否する。 |
| requestId | 1〜UInt64最大値。関連付け用であり、認証・重複排除ではない。 |
| payload | Pingは`{"nonce": UInt64}`、SetReadyは`{"ready": Bool}`。 |

整数はInt64 / UInt64の非負範囲だけを受理し、Float64 / Bool / Stringは変換しない。
nonceは0〜UInt64最大値。readyはBoolのみ。送信Writerは既存の決定的MessagePackを使う。
ProtocolVersionと型検証の成功後だけ、一時値から呼び出し側の出力へCommitする。
Decodeは所有Documentを作り、Queueには所有する整数・Boolだけを入れる。
DataElementや入力string_viewを呼び出し後に保持しない。

StreamはMessagePack長（Prefixを除く）を4 byte big-endianで先行させる。
0長と4,096 byte超はPrefix受理時にBody確保前に拒否する。
受信Chunkの境界はFrame境界ではなく、1 byte分割・複数Frame結合とも処理する。
Pendingには最大1 FrameのBodyと4 byte Prefixだけを保持し、Chunk全体はコピーしない。
DatagramはPrefixなしでMessagePackを1個だけ持ち、空・1,200 byte超・連結Messageは拒否する。
サイズ上限はこの試験Protocolの選択であり、経路MTUや断片化回避を保証する値ではない。
Parserは最大32 Token、深さ4、各Container 8要素に制限する。

## Queue・Thread・SessionのLifetime

`ControlMessageReceiver`は1接続/Session用。Transportは生成時に固定し、混在利用を拒否する。
mutexでReceive / TryPop / Disconnect / 診断取得を直列化する。
WorkerはReceiveで受信値を検証・Queue投入し、Main Thread等の利用側がTryPopしてDispatchする。
HandlerはDispatch中だけ借用し、その呼び出しThreadで実行する。Queue Lock内にCallbackはない。
Handler例外は利用側へ伝播し、自動再実行しない。Scene / GPUへの反映はHandler側の責務。

QueueはFIFO、既定64件・受信MessagePack長合計64 KiB。実際のメモリには型・deque等の管理領域も加わる。
Limitsは生成時に固定し、0容量は投入不可。上限超過時は待機・無制限蓄積をせずSessionを失敗させる。
Policyは生成時のAllowPing / AllowSetReadyで制限する。受信時とDispatch時の双方で適用できる。
これは型の許可であり、利用者認証・接続先の権限判定ではない。

不正Frame、Policy違反、確保失敗、容量超過はFailedとし、Pendingと未処理Queueを全破棄する。
同じReceive呼び出し内で先行した正常Frameも、未取り出しなら破棄する。
エラーはstickyで、その後の受信は拒否する。診断は構文位置と型変換のフィールド経路を分離する。
Disconnectは未処理データを破棄し、既存の失敗原因を維持する。繰り返し通知できる。
FinishStreamはEOF通知で、未完Prefix/BodyがあればTruncated、なければClosed。どちらもQueueを破棄する。
EOFより先に配送したい正常値は、利用側が先にTryPopする必要がある。

TryPop済みの値は利用側が所有し、Disconnectで取り消されない。
再接続は新しいReceiverを生成し、旧Sessionとの対応・取り出し後の取消は利用側で管理する。
破棄前にProducer/Consumerを停止・joinすること。破棄と同時のメソッド呼び出しは許可しない。
1接続につきReceive Producerは1つとし、呼び出し順をWire順に保つ。
複数ConsumerのCallback実行順までFIFOにする機構はない。

## 検証と残課題

`scripts/test-windows-structured-data.ps1`でData回帰とNetwork CPUテストを実行する。
全分割位置・1 byte受信・結合Frame・切断・不正Envelope・容量超過・Workerから呼び出し側Threadへの
FIFO Dispatchを対象とする。実Socketを使わないメモリ内Loopbackである。

2026-11-14の結果: MSVC CPUテストは既存5,643＋Network 429＝6,072チェック成功、警告0。
アプリDebug x64の増分ビルドも`RavenGenerateLinkDebugInformation=false`で成功。
既存CRT競合のLNK4098が1件。通常PDB生成はPhase 10で完了しなかった環境のため今回再試行していない。
GUIと実ネットワークでの動作は検証していない。

高頻度同期は未導入。現段階で圧縮・Quantization・Deltaを追加せず、実用途・通信層が決まった段階で
MessagePackのByte数・Encode/Decode時間・帯域を計測し、必要な場合だけ別Codecを検討する。
実Socket、認証・暗号化・再送、複数接続、実運用Lobby / RPC、性能閾値、Sanitizer / ThreadSanitizer、
確保失敗注入は別段階。接続数全体のメモリ予算・Rate Limitも利用側で追加が必要。
