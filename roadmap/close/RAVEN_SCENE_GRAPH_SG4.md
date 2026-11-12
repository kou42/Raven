# Scene Graph SG4 — Component View / System実行の計測改善

## 状態と範囲

基準はPR #366統合後のmaster `d9083d9e3366556c83205b2ab57320183278743a`。
ユーザー承認済みSG4のみを `feature/scene-graph-sg4` で実施した。
型付きStorageの非所有cacheを採用し、Debug / ReleaseのCPU計測・回帰確認を完了した。
SG5へは進まない。SG2/SG3から残る実機描画・Demo・長時間実負荷の確認は未検証のまま引き継ぐ。

## 現在の構造・問題・採用した変更

既存Viewは最小Storageを候補列に選ぶが、各候補の照合とComponent取得でSceneの型Mapを繰り返し検索していた。
3成分が16,384件全一致する場合、1走査で型検索98,307回、Filter照合49,152回、取得時照合49,152回だった。

- View生成時に型付きStorageのpointer tupleを一度取得する。Sceneがunique_ptrで所有するStorageのpointeeはMapのrehashで移動しない。
- 候補StorageはDense indexから所属が確定するため、Filterの再照合を省略し、Component取得も同じDense indexを使用する。
- 残るStorageはcache済みpointerからHas / 検証付きGetを呼ぶ。Entity生存確認と、Dense/Sparseの境界確認は維持する。
- View / IteratorはComponent配列のpointerや参照を保存しない。Iteratorは必要な状態を値で保持し、一時Viewの寿命にも依存しない。
- 公開互換Constructorも指定SceneからStorageを解決する。別Sceneや外部Storageを候補として渡す操作は拒否する。
- `Size()`は一致件数を走査して返す。`CandidateCount()`とは区別する。

これにより3成分Queryの型検索は生成時の3回だけとなる。同じ全一致入力のFilter / 取得時照合は各32,768回に減った。
型付きのViewに対する既存呼び出し箇所を検索し、通常のrange-for利用を維持している。
テンプレート本体は既存のScene.inlに置く。Sceneの完全定義が必要になるため、今回もComponentView.h末尾へ実装を移す構成変更は行わない。

## 計測条件

- Windows x64、MSVC v143（14.44）、同じコンパイル・リンク設定のDebug / Releaseを比較。
- 4,096 / 16,384 Entity、単成分・全一致3成分・末尾の成分が最小・疎なJoin・不一致・Storage欠落・実型のRigid Joinの7種類、計14条件。
- Rigid JoinはTransform / RigidBody / Colliderを使用する。その他はQuery自体のコストを分離する小さいテストComponent。
- 各条件でwarm-up後に5 sample × 8走査。表は1走査あたりの中央値（µs）。min/maxもログに残す。
- 計数・Heap計測は別passとし、時間のpassでは統計pointerを渡さない。型検索の削減と走査結果はCounter / Checksumで別に照合する。
- 比較表は親プロセスのaffinityをmask=1へ固定し、子プロセスへ継承して取得した。同一論理CPUを使用し、固定していない初回値とは混ぜない。
- 基準実装はSG3に計測Counter / CLIだけを追加した段階。cache / Version検証は未導入。純粋なmasterバイナリとの比較ではない。
- 基準の補助`alive_checks`はComponent取得APIの検証も含めていたため前後比較には使わない。現行のAliveChecksはViewからの明示的なIsEntityAlive呼び出しだけを表す。
- 作成・破棄・解放の時刻は別区間の単回値。CPU割込みやallocator状態の影響があるため、その大小から改善を主張しない。
- 数値はsynthetic CPU Queryの結果であり、実Demo全体のFPSやGPU性能の改善を意味しない。時間閾値をテストの合否条件にしない。

基準/変更後の候補数、一致数、Checksumは全条件で一致し、入力から計算した期待値とも照合した。

### 16,384 Entityの走査中央値

| 条件 | Debug前 µs | Debug後 µs | Release前 µs | Release後 µs |
|---|---:|---:|---:|---:|
| 単成分 | 16,014.9 | 4,349.31 | 946.725 | 179.500 |
| 全一致3成分 | 43,806.3 | 7,607.69 | 2,869.45 | 280.975 |
| 末尾が最小Storage | 10,748.0 | 1,832.05 | 680.562 | 81.2375 |
| 疎なJoin | 3,061.56 | 489.262 | 213.900 | 25.150 |
| 不一致 | 3,375.65 | 362.350 | 225.412 | 24.100 |
| Storage欠落 | 1.1875 | 1.8875 | 0.1000 | 0.1000 |
| 実型Rigid Join（4,096件一致） | 13,011.5 | 764.112 | 634.163 | 88.5125 |

全一致3成分はReleaseで約10.2倍、実型Rigid Joinは約7.2倍。Debugも検証処理込みで走査が短縮した。
Storage欠落では走査対象がなく、改善はない。DebugではVersion検証等の固定費が増えて約0.7µs遅くなった。
たとえば全一致Releaseの5 sampleの範囲は基準2,709.14〜5,516.77µs、変更後280.825〜295.825µsであり、外乱も含む記録として扱う。

### 生成・破棄・解放（16,384件の全一致条件）

| 区間 | Debug前 µs | Debug後 µs | Release前 µs | Release後 µs |
|---|---:|---:|---:|---:|
| Scene/Entity/Component作成 | 124,071 | 129,342 | 4,421.8 | 4,797.3 |
| 全EntityをQueueDestroy→Flush | 41,126 | 44,106.5 | 1,164.1 | 1,055.4 |
| 残るScene/Storage容量の解放 | 464.4 | 510.2 | 367.4 | 299.0 |

Debugは構造変更Versionの書き込みとStorage自体のサイズ増加も伴う。生成破棄の高速化は採用理由にしていない。

## メモリと計測の固定費

| sizeof（3成分View） | 計測用基準 | 変更後 |
|---|---:|---:|
| Debug View / Iterator | 24 / 32 byte | 80 / 88 byte |
| Release View / Iterator | 24 / 32 byte | 48 / 56 byte |
| Debug ComponentStorage | 104 byte | 112 byte |
| Release ComponentStorage | 80 byte | 80 byte |

計測用基準にも任意Counter pointerの8 byteが含まれる。計測フック追加前のView / Iteratorはpointer数から16 / 24 byteの構成だった。
cacheとDebug検証は主にstack上のView / Iteratorを大きくする。Component payloadや永続的なQuery cacheは追加しない。

既存のAllocationMeasurement.cppだけを `RAVEN_ENABLE_ALLOCATION_MEASUREMENT=1` でコンパイルして差し替えたRelease計測用実行ファイルでも前後を比較した。
マクロの利用箇所は同cpp内だけであり、他ObjectとComponentのレイアウトは通常Releaseと同じ。リンク後に計測Available=trueを確認した。
対象は呼び出しThreadのC++ new / aligned newの累積要求量であり、RSS、malloc、Driver、別Threadの確保ではない。

| 16,384 Entity | 前 | 後 |
|---|---:|---:|
| 全一致3成分の作成確保件数 | 420 | 420 |
| 同じ作成区間の累積要求byte | 9,274,676 | 9,274,676 |
| 実型Rigid Joinの作成確保件数 | 328 | 328 |
| 同じ作成区間の累積要求byte | 10,310,895 | 10,310,895 |
| 走査passの確保件数（14条件すべて） | 0 | 0 |
| QueueDestroy→Flush区間の確保件数 | 50 | 50 |

通常ビルドのAvailable=false時のCount=0を、確保0件の証拠として使わない。
計数passの時間は固定CPU・全一致Releaseで約314µs（時間passは約281µs）だった。計数passは1 sampleなので固定費の厳密な差分ではない。

## 構造変更・Lifetimeの契約

SceneがStorageを所有し、View / Iterator / 統計pointerは非所有。Sceneと統計先は利用中のViewより長く生存させる。
値編集、対象外Storageの作成、Scene型Mapのrehash、QueueDestroyEntityの予約は走査を継続できる。

Debugでは参加Storageの構造Versionを保存し、begin/end/CandidateCount、Iteratorの進行・参照時に確認する。
追加、削除、Clear、参照を再配置するReserve後の古いViewはlogic_errorとなる。
失敗した構造変更でも参照再配置の可能性があるため、Versionは保守的に進める。
Scene::OnDestroyではStorageの解放前にLifetime Versionを更新し、解放済みStorageのVersionを先に読まない。

ReleaseではVersionフィールド/検証はコンパイルしない。元からの「構造変更後はViewと参照を再取得する」契約を守る必要がある。
Debugも取得済みのraw Component参照の直接利用、Sceneオブジェクト破棄後のアクセス、並列の構造変更までは安全化しない。
Storage未作成時に得た空Viewも、後からStorageを作成したら再取得する。

## Systemの順序・同期境界

| 境界 | 入出力と今回の判断 |
|---|---|
| Game Logic → Animation | Gameplay設定を反映してからPoseを評価する既存順序を維持 |
| Animation → World評価 → Fixed Step | Kinematic等が最新Worldを読む既存のSG3同期を維持 |
| Fixed Step内 | Astro/Field→Rigid→Fluid/Soft→Thermalの順序を変更しない |
| catch-up終了 → 出力同期 | 0 Stepでは同期せず、複数StepでもSoft出力同期は1回 |
| Physics → Layer | Layerが確定した出力を読む |
| Layer → Destroy Queue Flush → World評価 | 走査中の破棄予約を、この既存境界で適用 |
| Pause | Game Logicと破棄/World評価のみ。Animation/Physics/Layerは停止 |

実際のAnimation PoseとSoftBody Participant / Layerの観測ログで0 Step / 2 Step / Pauseを検証した。
2 Stepが観測されたframeのProfiler FixedStep.Count=2とOutputSynchronization scopeも照合した。
Queryの型検索が計測上の改善対象であり、新しい並列実行や一般的な構造変更の遅延適用を必要とする根拠は得ていない。
今回は既存破棄Queueを維持し、Scheduler、追加の同期Barrier、汎用Command Bufferは導入しない。

## ビルド・回帰結果

- Windows Debug / Release x64ビルド成功。MSPDB不一致に対する既存の `/Z7`、`RavenGenerateLinkDebugInformation=false` と検証用GLFW native header IncludeをSG3同様に使用。
- Storage / TransformMath / Lifecycle / Transform / View / Simulation / PhysicsCollision / SceneGraph / Editor / 階層計測の全回帰成功。
- Releaseでも新しいView / System順序テストを実行。assertに依存しないRequireで検証する。構造変更の誤用テストだけはDebug限定。
- 新テストには全一致/疎/不一致、成分順、重複型、最小Storage、統計照合、一時ViewからのIterator、外部Storage拒否、Map再構築、swap-and-pop、破棄予約、同Indexの世代再利用、Shutdown後の拒否を含む。
- CPU UI Debugビルド成功。サンドボックス内では既存Dock保存テストが一時ファイル保存で失敗したため、同じRavenUITests.exeを外で再実行し終了コード0を確認。UI実装の変更はしていない。
- 最終の増分ビルドは指定上書きD9025と既存CRT混在LNK4098。全体再コンパイル時の既存Astro C4244は維持し、新しいテストのint→float警告は修正した。
- PowerShellの構文解析とfiltersのXML解析が成功。CPU固定を指定する測定スクリプトが終了時にaffinityを復元することも確認した。
- CIへDebug / Releaseの計測を追加。CI自体の実行結果はまだ確認していない。

主なログは `artifacts/logs/sg4-view-{baseline,optimized}-{Debug,Release}-pinned.log`、
`sg4-view-{baseline,optimized}-Release-allocation-pinned.log`、`sg4-full-regression.log`、`sg4-ui-debug-unsandboxed.log`。
基準の実行ファイルもartifacts/sg4に保存した。artifactsはGitへ含めない。

## 再実行

通常の依存環境では以下を使用する。Labelはログ名に使用し、数値の合否判定はしない。

```powershell
./scripts/test-windows-scene.ps1 -SkipBootstrap
./scripts/measure-windows-scene-view.ps1 -Configuration Debug -Label current -PinToFirstAllowedProcessor -SkipBootstrap
./scripts/measure-windows-scene-view.ps1 -Configuration Release -Label current -PinToFirstAllowedProcessor -SkipBootstrap
# C++ newの計測は独立した構成へ出力する。通常ビルドと混同しない。
./scripts/measure-windows-scene-view.ps1 -Configuration Release -Label memory -AllocationProfiling -PinToFirstAllowedProcessor -SkipBootstrap
```

ビルド済みなら-SkipBuildを指定できる。CLIは `--test-scene-view` / `--measure-scene-view`。
Runtimeで特定Queryを診断する場合はCallerがComponentViewStatisticsを保持してViewへ渡し、走査終了後に必要なCounterをProfilerへまとめて登録する。
構成を変更した箇所はScene/Testsの2ファイル追加だけで、structure.txtとvcxproj.filtersを同時に更新した。

## 残課題と次の判断

SG4のCPU計測と回帰確認は完了。Debugの固定費とViewサイズ増加は残るトレードオフとして明記する。
SG5のArchetype/Chunk採否は今回のQuery高速化だけでは決めない。実Scene負荷、抽出/同期/メモリを含めた計測が必要。
SG2/SG3の実機確認も別途残る。次のマイルストーンは再度の承認後に扱う。
