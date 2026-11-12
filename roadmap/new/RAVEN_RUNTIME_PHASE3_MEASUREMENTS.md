# Runtime Phase 03 — Scene Diagnostics / Performance

2026-11-13。既存Sceneを維持し、SG4のScene View計測とSG5のRuntime比較を再利用して基準値を取得した。
実行時のStorage、Query、Profilerの仕様や所有権は変更していない。
変更はSceneViewSelfTests.cppの計測処理と本記録・ロードマップのみ。Phase 04へ自動で進まない。

## 条件と計測の区別

- Windows x64、MSVC v143 14.44、Debug / Release。子プロセスを最初の許可CPU（mask=1）へ固定。
  CPUモデル・電源設定・クロックを固定した実験ではなく、バックグラウンド負荷の影響は残る。
- View: 4,096 / 16,384 Entity × 7配置、各5 sample × 8走査。warm-upとChecksum照合後に中央値を取得。
  空/不一致/疎なjoin、候補Storageが最後の型となる配置、実型RigidBody/Colliderのjoinを含む。
- Entityは全てScene::CreateEntityによりTag / Transformを持つ。singleはAをN個、denseはA/B/Cを各N個。
  last-smallestはA/BをN個・CをN/4個、sparse-joinはA/B/Cをそれぞれi%2/3/5==0のEntityに追加。
  disjointはAが偶数・Bが奇数・Cがi%4==0、missingはA/Bが各N個でCなし。
  rigid-joinはN/4 EntityにRigidBodyとColliderを追加し、Transformとjoinする。
- 通常走査、統計付き走査、CPUProfileScope + AddCounter有効、同じScope/Counter呼出しを無効化した走査を分ける。
  後3条件も5×8の中央値。後3条件の順はsampleごとに巡回するが、通常走査は先に測るため順序の影響を除去できない。
- Scope/CounterはEntityごとではなくQueryごと。BeginFrameと公開結果の照合は計時区間外。
  有効時のScope数・Counter件数（各8）とMatches合計、全modeのChecksumを検証する。
  これはCPUProfilerの収集費を含むQuery時間であり、Application Frame公開/UI描画全体の負荷ではない。
- 既存`counted_us`は単発から反復中央値へ変更。旧形式の単発時間は`diagnostic_pass_us`で明示する。
  新規`profiler_on_us` / `profiler_off_us`はScopeとCounterの両方の呼出費を含む。
- SG5: 4,096 / 32,768 Entity、Render所属切替0 / 1 / 20%、5 sample × 12 Frame。
  mirrorはSceneを正規とする固定型Chunk試作で、独立所有型Archetype Runtimeの測定ではない。
- 時間は通常ビルドの値を使い、C++ newの計測は別実行ファイルで行う。
  malloc、Driver、別Thread、GPU、RSS、allocator管理領域の合計は測定していない。

## Viewの時間と診断値

16,384 Entity。時間は1走査あたりµs、いずれも5 sample中央値。

| 配置 | 候補 / 一致 | Debug | Release | 統計ありRelease | Profiler有効 / 無効Release |
| --- | ---: | ---: | ---: | ---: | ---: |
| single | 16,384 / 16,384 | 4,545.09 | 174.85 | 181.60 | 178.00 / 176.61 |
| dense | 16,384 / 16,384 | 7,538.21 | 286.09 | 314.19 | 278.20 / 281.83 |
| last-smallest | 4,096 / 4,096 | 1,899.58 | 73.48 | 83.84 | 73.81 / 77.84 |
| sparse-join | 3,277 / 547 | 502.83 | 24.49 | 31.19 | 25.14 / 26.44 |
| disjoint | 4,096 / 0 | 373.60 | 23.19 | 26.53 | 23.56 / 23.20 |
| missing | 0 / 0 | 1.825 | 0.0875 | 0.10 | 0.25 / 0.10 |
| rigid-join | 4,096 / 4,096 | 691.31 | 89.90 | 98.81 | 92.59 / 90.13 |

候補/一致/ComponentReadsを実際の走査結果と照合済み。Debugはstructural version検証を含み、
Releaseのstructural_checksは0。denseのReleaseは3 Storage lookup、32,768 filter checks、
32,768 read checks、16,384 alive checks。単一型は重複する所属照合を行わない。

同fixtureでもDebugはReleaseの約8〜26倍。これはビルド条件とDebug安全検証を含む差であり、
Debugのフレーム時間からReleaseの最適化要否を判断しない。
統計付き走査は通常走査より増える傾向があるが、Profiler onがoffを下回る配置もある。
負の計測コストとは解釈せず、微小なScope負荷の正確な割合は今回の変動幅では確定しない。
空Queryの0.1µs前後は時計・呼出し固定費に近く、倍率を性能改善の根拠にしない。

View / Iterator / Storageオブジェクト本体のsizeofはDebugが80 / 88 / 112 bytes、
Releaseが48 / 56 / 80 bytes（Viewは3型）。vectorの動的領域やScene総保持量を含まない。

## 確保と保持メモリ

別のRelease計測実行ファイルでallocation_available=1を確認。全14配置で走査中C++ newは0件。
通常ビルドのallocation_available=0・Count=0は「未計測」であり、この結論の根拠にしない。

16,384 Entityのsetupの累積new要求値（解放分を差し引かない）:

| 配置 | 確保件数 | 累積要求bytes | 破棄区間の確保件数 |
| --- | ---: | ---: | ---: |
| single | 268 | 7,087,280 | 50 |
| dense | 420 | 9,274,676 | 50 |
| last-smallest | 412 | 8,882,101 | 50 |
| sparse-join | 400 | 8,128,218 | 50 |
| disjoint | 406 | 8,513,912 | 50 |
| missing | 344 | 8,180,978 | 50 |
| rigid-join | 328 | 10,310,895 | 50 |

setupにはScene、fixtureのEntity vector、Storage成長等を含む。破棄はQueue予約とFlushの合計。
確保箇所ごとの帰属は未測定なので、50件の原因を特定したとは扱わない。
Scene全体の保持メモリを測る新APIは追加せず、累積確保量と区別して未測定とする。

## SG5の再計測

Release、µs。Frameは所属変更を除く更新+Query、mirrorは入力copy+kernel+書戻し。

| Entity数 / 切替率 | Sparse Frame | mirror Frame | Sparse構造変更/Frame | mirror構造変更/Frame |
| --- | ---: | ---: | ---: | ---: |
| 4,096 / 0% | 75.90 | 230.60 | 0.025 | 0 |
| 4,096 / 1% | 76.53 | 228.85 | 3.63 | 6.78 |
| 4,096 / 20% | 75.80 | 229.25 | 69.04 | 130.97 |
| 32,768 / 0% | 604.13 | 1,814.57 | 0.017 | 0.008 |
| 32,768 / 1% | 589.64 | 1,799.94 | 26.98 | 49.78 |
| 32,768 / 20% | 659.02 | 1,848.81 | 557.62 | 1,044.75 |

32,768 / 0%では入力903.86、kernel62.47、書戻し844.14µs。
Sparseの生成7,600.60 / 破棄1,788.80µsに対し、Scene+mirrorの生成13,525.30 / 通知付き破棄2,121.80µs。
各区間は独立した中央値で、合計はFrame中央値と厳密には一致しない。
DebugでもChecksum照合は成功したが、今回の実時間には変動があり、Releaseとの比率を普遍化しない。

mirrorの追加保持は4,096 Entityで419,056〜434,432 bytes、32,768で3,281,360〜3,296,736 bytes。
これはcapacity×sizeofの集計であり、正規Scene、Asset、allocator管理領域は別。
5 sample合計の生成new要求bytesは、4,096でSparse 11,392,435 / mirror 15,219,585、
32,768でSparse 86,500,370 / mirror 116,042,415。保持量ではない。

Snapshot抽出は32,768件で新規vector 2,847.63µs、capacity再利用2,300.21µs。
確保計測では新規1件 / 再利用0件、再利用vectorの保持容量4,980,736 bytes。
通常のFrame経路に再利用を自動適用した結果でも、GPU Upload/Presentを含む結果でもない。

## 判断と残課題

- 全量mirrorは今回もReleaseで約2.8〜3.1倍のFrame時間。SG5の不採用判断を維持し、独立Runtimeを新設しない。
- Viewの定常走査にnewはなく、今回の値からallocator変更を提案する根拠はない。
- 統計は必要なpassだけで取得し、Profiler Scope/CounterをEntityごとへ増やさない既存方針を維持する。
- 生成/破棄や高い構成変更率の負荷は見えるが、実Demoのボトルネックと確認してからReserveやバッチ化を検討する。
- Snapshotのcapacity再利用はCPU抽出の候補。Frame寿命・GPU fence・Resize/失敗処理・実機負荷はPhase 04で確認する。
- 時刻や倍率を自動テストの合否閾値にしない。Checksumと件数・資源の整合を合否条件とする。

## 再実行と検証

通常計測（既存スクリプト、各Configurationで実行）:

```powershell
./scripts/measure-windows-scene-view.ps1 -Configuration Release -Label phase3 -PinToFirstAllowedProcessor -SkipBuild -SkipBootstrap
./scripts/measure-windows-scene-runtime.ps1 -Configuration Release -Label phase3 -PinToFirstAllowedProcessor -SkipBuild -SkipBootstrap
```

- Debug / Releaseビルドは終了コード0。既存ローカルGLFWヘッダーをCLのincludeへ追加し、
  PDBツール不整合のためRavenGenerateLinkDebugInformation=false / LinkIncremental=falseを指定した。
  Debugの既存リンクWarningはLNK4075 / LNK4098、ReleaseはWarning 0件。
- 通常計測はDebug / Release各14 View条件、各6 world条件と2 Snapshot条件が終了コード0。
  各入口の既存SelfTests、追加したProfiler Scope/Counter照合も成功。
- 確保計測は現Releaseのlink tlogからresponse fileを作り、現ソースのAllocationMeasurement.cppだけを
  MSVC 14.44 /MD /O2 /DNDEBUG /std:c++latestとRAVEN_ENABLE_ALLOCATION_MEASUREMENT=1で再コンパイル。
  現Release object群の同objectを差し替えて別exeへリンクした（終了コード0）。通常exe/objectは変更しない。
  artifacts/phase3/allocation.rspとallocation-Release.exeへ保存。古いSG5 exeの再利用ではない。
  PATHに通常ReleaseのDLL配置を追加し、mask=1で両計測を実行。allocation_available=1と終了コード0を確認。
  再現時は対応する同一ソース/ツールセットのobject群を使用し、確保計測ありの時間を通常時間へ混ぜない。
- DebugのSceneテストスクリプトは2 standalone group + 10入口すべて成功。
- ログ: artifacts/logs/runtime-phase3-{Debug,Release}-build.log、runtime-phase3-debug-tests.log、
  sg4-view-phase3-{Debug,Release}.log、sg5-runtime-phase3-{Debug,Release}.log、
  runtime-phase3-allocation-build.log、phase3-allocation-{view,runtime}.log。
- Raven配下の追加・削除・移動はないため、structure.txt / VS filtersへの登録追加は不要。

実機GPU、全Demo、長時間負荷、Scene総保持メモリ、汎用Archetype、並列Queryは未検証。
以上の条件内で変更要否を判断する基準値を取得し、Phase 03を完了とする。
