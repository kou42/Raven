# Phase 8 計測結果（2026-11-13）

Windows x64 / Intel64 Family 6 Model 165 Stepping 5 / 論理Processor 20。
MSVC 19.51.36260 / C++17 / O2 / NDEBUG。GPU・GUI・File I/Oは計測対象外。

## 再実行

リポジトリルートから順番に実行します。同じ出力領域を使うため同時実行しないでください。

```powershell
./scripts/test-windows-structured-data.ps1
./scripts/test-windows-structured-data.ps1 -Benchmark
./scripts/test-windows-structured-data.ps1 -Benchmark -MeasureMemory
```

生成先は`artifacts/structured-data/benchmark-time.csv`と`benchmark-memory.csv`。
今回の生データは同じディレクトリの`Phase8Time.csv`と`Phase8Memory.csv`に保存した。
通常テストは計測・global new置換なし。時間計測もglobal newを置換しない。
メモリ計測だけ専用macroでnew/deleteを置換し、Headerによるサイズ・世代追跡を行う。

## 測定の定義

- 時間: 1回warm-up後の7回の中央値。入力生成とPool生成は含めず、各処理の一時値・出力の破棄は含む。
  CPU affinity固定や負荷隔離はしておらず、同じ環境でも値は変動する。単発のCI合否判定には使わない。
- Parse: 入力コピー、構文検証、Tape生成、Document確定、破棄まで。
- Decode: 全String（Objectキーを含む）を走査して所有Stringへ取得・保持・破棄する。
  純粋なEscape展開だけの計測ではなく、Traversalと未Escape文字列のコピーも含む。
- Convert: 配列は専用設定型への逐次Deserialize。deep96は96段のArrayを降りて末端整数を取得する。
- Write: DocumentをData JSON Writerで出力し、生成文字列を破棄する。
- Allocation / Bytes: 計測区間に新しく要求したC++動的確保の件数と累積Byte数。Worker分も合算。
- Peak: 区間中に新規確保し、まだ解放していない要求Byteの同時最大値。累積Bytesとは異なる。
  区間開始時点の既存Document・Pool・入力、追跡Header、Stack、malloc直呼び、OS/GPUは含まない。
  ParseのPeakにはその処理で作ったDocumentが含まれる。他段階の既存Documentは含まれない。
  異なる区間で生まれた領域の遅延解放は世代で区別し、次区間のlive値を減らさない。
  並列時の解放タイミングによってPeakは変動する。メモリ追跡ビルドの時間は採用しない。

## 処理段階別

smallは設定1件・整数3個、largeは4,096件・各32整数、escapedは4,096件・各3整数とEscape付きlabel。
deep96は96段Array内の整数。すべてJSON Parserで検証済みの入力を用いる。

| 入力 | 段階 | Token数 | 中央値 µs | 確保回数 | 累積Bytes | Peak Bytes |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| small | parse | 15 | 2.4 | 21 | 3376 | 1224 |
| small | decode | 15 | 1.2 | 14 | 1440 | 720 |
| small | convert | 15 | 0.9 | 6 | 696 | 672 |
| small | write | 15 | 3.1 | 18 | 3031 | 1472 |
| large | parse | 176129 | 9295.9 | 65 | 30204860 | 12997501 |
| large | decode | 176129 | 4230.7 | 4145 | 4323969 | 2396069 |
| large | convert | 176129 | 7425.6 | 45058 | 2474599 | 950999 |
| large | write | 176129 | 11721.2 | 65564 | 13484014 | 1537584 |
| deep96 | parse | 97 | 6.2 | 41 | 30551 | 11319 |
| deep96 | decode | 97 | 1.2 | 1 | 16 | 16 |
| deep96 | convert | 97 | 0.8 | 0 | 0 | 0 |
| deep96 | write | 97 | 31.6 | 203 | 88115 | 33166 |
| escaped | parse | 57345 | 4714.4 | 62 | 12197223 | 4329885 |
| escaped | decode | 57345 | 2784.7 | 8241 | 4455041 | 2520933 |
| escaped | convert | 57345 | 3491.9 | 20482 | 623207 | 442991 |
| escaped | write | 57345 | 7683.2 | 53276 | 10322529 | 949324 |

## 配列の逐次・並列比較

配列要素はPhase 7と同じTestSettingsとAdapter。labelはEscape文字列。
既存経路は従来の`Deserialize(vector<T>)`、新逐次は`DeserializeArray`の逐次経路。
Poolは1 / 2 / 4 / 8 Workerで測定し、ここでは4 / 8 Workerの結果を抜粋した。
全要素の値・順序を既存経路と照合してから測定する。全比較行はCSV参照。

| 要素数 | 要素内整数数 | Token数 | 既存 µs | 新逐次 µs | 4 Worker µs | 8 Worker µs | 実測policy µs |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 32 | 0 | 353 | 21.4 | 19.9 | 26.8 | 25.4 | 20.1 |
| 128 | 0 | 1409 | 83.9 | 79.8 | 40.3 | 33.0 | 34.2 |
| 512 | 0 | 5633 | 339.7 | 314.3 | 162.4 | 112.2 | 125.4 |
| 2048 | 0 | 22529 | 1402.8 | 1257.2 | 508.7 | 377.9 | 464.3 |
| 8192 | 0 | 90113 | 5674.8 | 5305.3 | 1783.0 | 1205.8 | 1753.1 |
| 32 | 64 | 2401 | 92.6 | 92.2 | 66.5 | 38.4 | 91.7 |
| 128 | 64 | 9601 | 367.7 | 362.5 | 169.5 | 110.0 | 118.3 |
| 512 | 64 | 38401 | 1543.0 | 1499.4 | 511.5 | 373.0 | 502.4 |
| 2048 | 64 | 153601 | 6217.2 | 5879.8 | 1816.7 | 1135.3 | 1824.4 |
| 8192 | 64 | 614401 | 25147.8 | 24681.8 | 7217.0 | 4416.1 | 7190.8 |

軽い32件は新逐次19.9 µsに対して4 Worker 26.8 µsと遅いため、実測policyでは逐次を維持する。
128件・1,409 Tokenでは新逐次79.8 µs、4 Worker 40.3 µsで改善したため、今回の設定型向けには
`EnableParallel=true, MinElements=128, MinTokens=1409`を4 Worker Poolに指定する条件を評価した。
32件・各64整数の入力も保守的に逐次に残す。8,192件・各64整数では既存25.15 msに対し
8 Worker 4.42 ms（約5.7倍）、実測policyの4 Workerは7.19 ms（約3.5倍）だった。

これらはこの設定型・実機の試験値であり、汎用APIへハードコードしない。
Adapterの処理量やWorkerの利用状況が変わったら再計測する。APIは既定で並列無効。
要素数とToken数の両方を満たすと並列化し、Token数は対象Array自身を含む部分木全体で数える。

8,192件・各64整数のメモリは、既存Peak 4,596,742 Byte、新逐次3,867,491 Byte、
8 Worker 3,872,546 Byte。配列の出力を一度だけ確保し、Worker数分の範囲・future・診断を保持する。
要素ごとのJobや全Token索引は作らない。キー索引・Token圧縮は、今回の改善条件に不要のため追加していない。

## 正しさ・制限

既存5,403チェックと新規37チェックが通常MSVCテストで成功（合計5,440、警告0）。
メモリ専用ビルドでは新規50チェックが成功し、出力領域確保からSubmit途中までの確保失敗を注入した。
別途、既知サイズのPeak、Aligned確保、前区間領域の解放、Worker確保・別Worker解放を検証した。
順序、端数分割、空・1要素、閾値境界、1 Worker、型エラー集約、入れ子の最小index経路、
開始前・途中取消、Adapter例外、出力維持、Pool再利用を確認した。

並列APIは呼び出し元がPoolのWorkerではないこと、Document・Pool・取消Flagが終了まで生存することが前提。
全futureを回収してから返る。取消は要素間で確認し、実行中Adapterの強制停止はしない。
複数エラーは全失敗件数と最小indexの診断を返す。取消時の件数は処理済み範囲だけ。
Adapterは共有状態を変更せず、独立した所有データだけを生成する必要がある。
共有Lazy Cacheは導入しない。安定IDの参照解決やScene/GPU反映は呼び出し側で成功後に行う。
`vector<bool>`は要素間で保存領域を共有するため拒否する。既存Deserializeの動作は変更しない。

アプリDebug増分ビルドは既存のPDB生成無効オプションでリンク成功。既存LNK4098が1件。
通常PDB生成の既知のMSPDB140.DLL不一致は未解消。GUI、Sanitizer、ThreadSanitizer、
他CPU・Compilerでの閾値、ゲーム実行中の同時負荷は未検証。
Diagnostics UIへの常時出力は追加せず、専用実行時だけCSVへ集計出力する。
