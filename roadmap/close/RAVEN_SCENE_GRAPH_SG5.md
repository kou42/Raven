# Scene Graph SG5 — RuntimeWorldとの接続判断

## 結論と範囲

PR #367統合後のmaster `5d5c41b887ccb22bded11cf8a69c27e51f714191`を基準に、
承認済みSG5だけを`feature/scene-graph-sg5`で実施した。

**SceneのSparse Setを維持し、Sceneと全成分を二重保持するRuntimeWorldは導入しない。**
今回の固定型Chunkは単体走査こそ速いが、入力同期と書戻しを含むとReleaseで約3倍の時間がかかった。
この結果は「今回の負荷で毎Frame全量同期するmirror方式」の判断であり、Archetype一般の否定ではない。
大量の同型Entityを独立所有し、複数Systemで繰り返し処理する新要件が生じた場合は再評価する。

描画には既に値とGPU Resource参照のSnapshot境界があるため、新しいRenderWorldを重ねない。
出力vectorを継続所有する利用者向けに、`RHISceneDrawItemBuilder::BuildInto`だけを追加した。
既存`Build`は互換入口として共通実装へ委譲し、`Renderer::BuildRHISceneDrawItems`も同じ経路を使う。
ただし通常の`PrepareRHISceneFrame`は新しいFrame/vectorを構築するため、**通常描画全体の確保削減やFPS改善は未実証**。
Prepared Frameのpool化、非同期実行、GPU fence、各BackendのFrame寿命は変更していない。

## 所有権と同期の決定

| データ | 正規データ / 所有者 | 同期方向・タイミング / 破棄 |
|---|---|---|
| Entity Index / Generation、Tag、Relationship、Component所属 | Scene | Sceneだけが生成破棄。非所有Entity/ViewはSceneより短命。別Worldに同じallocatorを作らない |
| Local Transformと階層評価 | Scene / SceneTransformSystem | 編集・Gameplay入力 → 親から子へWorld評価。既存SG2のWorld互換契約を維持 |
| Static / Kinematic姿勢 | Scene / Gameplay・Animation入力 | Fixed Step前に評価WorldをPhysicsへ。Kinematicは既存の速度・搬送同期を使う |
| Dynamic姿勢・速度、Solver内部 | PhysicsSimulationWorld / PhysicsWorld | Fixed Step後にPhysics World姿勢 → Scene World/Local。Sceneの親移動でDynamic姿勢を上書きしない |
| Animator状態・Pose・Animation内部バッファ | Animator / Animation subsystem | 既存Scene更新順で評価し、対象Sceneへ反映。Root Motionの新規抽出/消費は未実装のまま |
| Mesh / Materialの編集値・Asset | SceneのComponentと既存Asset / Resource所有者 | Render準備時に評価済みWorld、Material値、Resourceの共有参照を抽出。Asset自体を複製しない |
| RHISceneMesh / RHISceneDrawItem | 抽出時点の値、ResourceはRef共有所有 | Scene → Frameの一方向。Scene/Component pointerを保持しない。Frame破棄/再抽出で参照解放。Sceneへ書戻さない |
| Chunk比較用Transform / Velocity / RenderPayload | テスト内のChunkMirror。Sceneが正規 | 構造変更通知 → 全入力copy → kernel → Sceneへ書戻し。実行中のScene変更なし。Runtimeには接続しない |

GPU PickingのEntityIDは既存どおりIndexのみで、CPU側でGenerationを再取得する。
GPU readback中のIndex再利用に関する既存制約は今回解決していない。
比較用Chunkは完全なEntityHandleを保持し、旧Generationの通知が新Entityを消さないことを確認した。

## 比較試作と測定条件

`Scene/Tests/SceneRuntimeComparisonTests.cpp`の無名namespace内だけに、2種類の固定型SoAを置いた。
128行/Chunk、MotionはHandle + 実型TransformComponent + Velocity、Renderはそれに非trivialな共有Asset参照とTintを追加する。
ReleaseのChunk本体はMotion 6,152 bytes、Render 9,224 bytes。16KiBを超えないが、完全に詰めた汎用Archetype allocatorではない。
空き行は型ごとの最初の空Chunkから再利用し、空Chunkもhigh-water capacityとして保持する。

- 4,096 / 32,768 Entity。初期Render所属率50%、毎Frameの所属切替0 / 1 / 20%。切替は決定的に巡回する。
- SG4後のScene Viewで位置Xを更新し、Transform + RenderPayloadをQueryするSparse Setと、同じSceneを正規データに持つChunk mirrorを比較。
- 同期を除いたkernel、全入力copy、全書戻し、Component追加削除/Chunk間移動、生成、Entity破棄通知を別区間で計測。
- 全6条件で5 sample × 12 Frame。Frame処理は1 Frame平均のsample中央値、生成/破棄は各sampleの一括処理中央値。時間を合否閾値にしない。
- 毎Frame同じ入力から両方式のChecksumを照合。テストでは複数Chunkのswap-remove後も非Render行を含むEntityごとの期待位置を検証する。
- Windows x64、MSVC v143 14.44、通常Debug / Release。同じ親PowerShellのCPU affinity mask=1を子に継承し、終了時に復元する。
- SIMD専用コード、Job並列化、汎用型登録、Chunk圧縮/解放、Physics/階層/Animation/GPU処理を含めない。
  親なしWorld-space互換Transformのsynthetic負荷であり、実Demoやゲーム全体の速度を示さない。
- Chunk側もSceneを生成・保持する。入力をdirty差分にした方式、Chunkを唯一の正規データにした方式は未計測。

## Release測定

通常Releaseの最終ログは`artifacts/logs/sg5-runtime-verified-Release.log`。
時刻はµs。下表のFrame処理は構造変更を除き、Sparse Setは更新+Query、mirrorは入力同期+kernel更新/Query+書戻し。
構造変更の時間は次表で別に示す。Scene同期のないkernelだけで採用を決めない。

| Entity数 | 切替率 | Sparse Frame | Chunk kernel | 入力同期 | 書戻し | mirror Frame | mirror追加保持bytes |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 4,096 | 0% | 76.00 | 6.76 | 115.15 | 111.39 | 232.34 | 419,056 |
| 4,096 | 1% | 75.40 | 6.58 | 113.26 | 117.41 | 237.53 | 425,208 |
| 4,096 | 20% | 76.74 | 7.01 | 117.67 | 120.36 | 245.72 | 434,432 |
| 32,768 | 0% | 620.71 | 63.58 | 924.57 | 878.53 | 1,862.25 | 3,281,360 |
| 32,768 | 1% | 633.65 | 63.99 | 925.52 | 937.05 | 1,925.50 | 3,287,512 |
| 32,768 | 20% | 660.95 | 89.11 | 974.55 | 941.82 | 1,998.72 | 3,296,736 |

| Entity数 / 切替率 | Sparse所属変更/Frame | mirror所属変更/Frame | Sparse生成 | Scene+mirror生成 | Sparse破棄 | Scene+通知破棄 |
|---|---:|---:|---:|---:|---:|---:|
| 4,096 / 0% | 0.03 | 0.02 | 1,182.50 | 1,899.30 | 252.20 | 268.80 |
| 4,096 / 1% | 3.52 | 6.62 | 1,000.50 | 1,768.80 | 222.20 | 257.90 |
| 4,096 / 20% | 68.45 | 131.54 | 1,011.10 | 1,610.40 | 225.70 | 257.20 |
| 32,768 / 0% | 0.03 | 0.01 | 7,948.40 | 13,845.90 | 1,861.80 | 2,114.60 |
| 32,768 / 1% | 27.93 | 52.63 | 7,695.90 | 14,022.00 | 1,869.30 | 2,194.80 |
| 32,768 / 20% | 548.33 | 1,049.37 | 7,858.00 | 13,831.30 | 1,912.70 | 2,308.90 |

各列は独立した中央値なので、区間の和がFrame中央値と一致するとは限らない。
0%変更の空ループ時刻は時計の固定費程度であり、性能差と扱わない。

Debugでも32,768 EntityのSparse Frame約20.0msに対しmirror約39.2〜39.8msとなり、
Debug検証を含めても全量mirrorを採る根拠はなかった。詳細はログに保存する。

### メモリとC++ new

追加保持bytesはChunk、bank vector、Generation付きLocation vectorのcapacityとsizeof本体の合計。
正規Scene、Resource本体、allocatorの管理領域、RSS、GPUメモリは含まない。
**mirrorの総メモリは元のSceneにこれらが加わる**。Sceneの保持メモリ総量を測った値ではない。

別のallocation計測ビルドで、生成区間のC++ new要求bytesを測定した。解放分を差し引かない累積値であり、保持メモリではない。
5 sample合計は4,096 EntityでSparse 11,392,435 / mirror 15,219,585 bytes、
32,768 EntityでSparse 86,500,370 / mirror 116,042,415 bytes。入力fixtureのEntity vectorも含む。
malloc、Driver、別Threadは対象外。通常ビルドの`allocation_available=0`と計測済み0件を区別する。

## Snapshotの共通経路

既存のRender Queue → RHISceneMesh → RHISceneDrawItemの境界、column-major行列、
`clipCorrection * projection * view * model`の積順、LocalCenterからのViewDepthを維持した。
`BuildInto`は毎回全件を再構築し、古いResource参照をclearする。dirty cacheやSceneへの逆方向同期ではない。
確保をclearより先に行い、確保失敗時にも以前の出力を保持する。
`Renderer::BuildRHISceneDrawItems`は全Mesh検証後だけ出力を更新し、false時の出力維持を変えない。
非テンプレートの実装は新しい`.cpp`へ移し、Visual Studio filters / structure.txtも更新した。

| Snapshot数 | 新規vector µs | capacity再利用 µs | 新規C++ new / 抽出 | warm再利用C++ new / 抽出 | 再利用capacity bytes |
|---:|---:|---:|---:|---:|---:|
| 4,096 | 305.66 | 269.73 | 1 | 0 | 622,592 |
| 32,768 | 2,807.32 | 2,249.75 | 1 | 0 | 4,980,736 |

時刻は通常Release、new件数は別計測pass。Resource Refコピー・各反復の参照解放を双方に含む。
GPU BufferはCPU test doubleで、Mesh準備・Upload・Presentは含まない。
高水位capacityを保持する代償があり、出力vectorの寿命を延ばす設計を全Backendに自動適用していない。
Frameのpool化にはGPU使用完了やresize/failure経路の寿命検証が必要なため、今後の別作業とする。

## 検証と再実行

新CPUテストはDebug / Release共通で`Require`を使用し、Releaseでも実行を省略しない。
別Scene拒否、重複追加、複数Chunkの構成移動、共通成分保持、swap-removeのLocation補正、
破棄後通知、Index再利用・旧Generation通知、空き行の非trivial参照解放を確認する。
Snapshotでは行列/深度の具体的期待値、Material値の独立性、Buffer寿命、空/減少/再増加入力、
互換Build、公開抽出APIのfalse時出力維持を確認する。

再実行入口:

```powershell
./scripts/test-windows-scene.ps1 -SkipBuild -SkipBootstrap
./scripts/measure-windows-scene-runtime.ps1 -Configuration Debug -Label current -PinToFirstAllowedProcessor -SkipBuild
./scripts/measure-windows-scene-runtime.ps1 -Configuration Release -Label current -PinToFirstAllowedProcessor -SkipBuild
# 別出力のallocation計測ビルドを作成する場合（通常のbuildスクリプト経由）
./scripts/measure-windows-scene-runtime.ps1 -Configuration Release -Label profile -AllocationProfiling -PinToFirstAllowedProcessor
```

Windows CIへDebugのCPU回帰入口とRelease比較を登録した。CIの性能数値を時間閾値で判定しない。
今回のローカルallocation確認は既存の計測用AllocationMeasurement objectを新しいRelease object群へ再リンクした別exeで実行した。
正常な通常Releaseと比べてコードを変更した比較ではなく、C++ new置換だけを有効化したもの。

ローカル検証結果（すべて終了コード0）:

- 通常Debug / Release x64ビルド。最終ログは`artifacts/logs/sg5-verified-Debug-build.log`、`sg5-verified-Release-build.log`。
- `test-windows-scene.ps1`: Storage / Transform Mathの2 standalone groupと、Lifecycle / Transform / Runtime比較 / View / Simulation / Physics Collision / Graph / Editor Commands / Graph計測の9入口が成功。
- SG5はDebug / Release両方で自己テスト成功。Debug / Releaseの全6 world条件と2 Snapshot条件でChecksum照合成功。
- Debug Explicit Frame、Animation Runtime、Character AnimationのCPU回帰が成功。
- Release CPU UI回帰成功。`artifacts/logs/windows-ui-test-results-release-x64.log`。
- allocation計測ビルドでも全条件が成功し、Snapshotのwarm確保0件を確認。`artifacts/logs/sg5-runtime-allocation-Release.log`。
- `git diff --check`、追加3ファイルの実在・structure.txt記載・VS filters登録とXML構文を確認。

ローカル依存の既存問題を避けるため、SG3/SG4と同じGLFW native headerのartifact include、`/Z7`、link debug情報無効を使用した。
リポジトリのBuild設定や依存ファイルは変更していない。Warningは既存UITextReflowTests / AstroテストのC4244、
ローカル指定によるD9025、初回Debug linkのLNK4098を確認し、新規SG5ソースからのWarningはなかった。
UIスクリプトはRelease buildのWarningを64件と報告した（ログ中の同一Warning再掲を含む集計）。
GPU描画、実機Demo、長時間性能、CI実行結果は未検証。CPUテスト成功で代替しない。

## 残課題と次の判断

SG5の接続判断でScene Graphロードマップの今回の作業を区切る。
次はSG2/SG3から残る実機GPU / Demo / 長時間負荷の検証を優先する。
Root Motionの抽出/消費、汎用RuntimeWorld、Scheduler、並列System、Backend横断のFrame poolは未実装である。
新しい大量同型Entityの要件や実測ボトルネックが得られた時だけ、別マイルストーンとして承認を得て再開する。
