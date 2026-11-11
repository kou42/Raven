# Scene Graph SG3 — Physics / Animation Transform同期

> 現在のCPU回帰は全成功。初回対応で残った移動床Crush / Box積み上げの失敗は、ユーザーの追加依頼により修正済み。
> 実機GPU / Demo / 長時間性能は未検証。初回の失敗再現と追加修正は以下に分けて記録する。

## 対象と基準

ユーザー承認済みのSG3だけを対象とする。基準は `391a7e02`。
このcheckoutは取得済み `origin/master` の `76fcad4d` に対し3コミット先行し、遅れはなかった。
既存のImGui撤去を含むため、その状態から `feature/scene-graph-sg3` を作成した。
PR準備時に最新参照を取得し、ImGui撤去が `origin/master` の `ad551b3f`（PR #365）へ統合済みであることを確認した。
基準コミットとそのmasterのファイル内容は同一であり、今回のPR差分へImGui撤去を重複して含めない。
SG4には進まない。SG2の実機描画比較は引き続き未検証。

## 所有権とデータフロー

| 対象 | 正規入力と同期 |
|---|---|
| Legacy Entity | 従来のWorld TRSを維持。Local opt-inは自動適用しない |
| Local Static / Colliderのみ | Localと親WorldからWorldを評価。StaticをSolverは移動しない |
| Local Kinematic | Gameplay / AnimationのLocal入力をWorldへ評価し、既存World速度積分後のWorldをLocalへ戻す |
| Local Dynamic | PhysicsのWorld姿勢が正規値。親が移動してもWorldを保持し、Localを逆算する |
| Animation | Legacyは従来World出力。Local opt-inではTrackをLocalとして適用。Dynamicは既存同様に更新対象外 |
| Deformation / SoftBody | Mesh/Particleの既存local-space契約を維持。Scene Worldを確定してから非所有Bindingを作る |

既存PhysicsWorldはTransformComponentを直接更新する。別の剛体姿勢バッファや所有Worldは追加しない。
SceneTransformSystemのキャッシュはScene所有の派生データであり、Component参照を永続保持しない。
Component追加の順序で制約を回避できないよう、LocalまたはRigidBody追加時に候補を読み取り検証する。
不成立時は候補Componentを取り除き、既存のLocal/World入力とキャッシュを変更しない。

DynamicのLocal直接編集は拒否する。意図的な姿勢変更はSetLocalTransform / SetWorldTransformで指定する。
Teleport / MovePositionは現在Worldを取得し、検証付きWorld設定を使う。Dynamic Teleportは従来どおり起床させる。
回転の明示設定ではPhysics Quaternionも同期する。Dynamicの通常評価ではSolver Quaternionを再生成しない。
Dynamic World→Local変換では再構成誤差を検証する一方、World窓口には元のPhysics値を残して静止姿勢の漂流を防ぐ。

## 同期タイミング

1. Game Logic → Animationの既存順序を維持し、Physicsへ渡す前に階層を評価する。0 Step frameでも評価する。
2. PhysicsSimulationWorldの各Fixed Step先頭でWorldを評価し、Astro / Fieldへ最新Worldを渡す。
3. 単独PhysicsWorld::Stepでも入力を評価する。Kinematic Quaternionは毎StepのWorld入力から更新する。
4. Rigid SolverのWorld出力をLocalへ同期し、描画子・子Colliderを更新してからFluid / Softへ渡す。
5. Fluid / Rigid Couplingの位置補正も同じFixed Step末尾で同期する。
6. RayCast / QueryAABB / CapsuleCastは親の直接変更を評価してから問い合わせる。
   Characterの移動床追跡も前GroundのWorld移動量を読む前に評価する。
7. 既存Layer更新と破棄キュー処理後の最終評価を維持する。

60Hz、catch-up上限、SoftBody / Fluidの出力同期をframe末尾1回へまとめる既存契約は変えない。
World行列再計算の既存Counterは再計算数のみを表す。追加した評価入口の走査時間は含めない。
Scale検証は親/自身の行列が同じ場合に前回の成功結果を再利用する。

## 座標・制約・失敗

- Sceneの行列積は従来どおり `World = ParentWorld * Local`、`Local = T * Rx * Ry * Rz * S`、角度はradian。
- Local AnimationのQuaternion→EulerはSceneと同じ積順へ変換する。Legacy Animationの変換結果は変更しない。
- Local RigidBodyは親Worldと自身Worldが正の一様Scaleで表せる範囲に限定する。
  非一様/反射Scaleを剛体・慣性へ暗黙に近似しない。ColliderのみのEntityは既存の形状別Scale契約を使う。
- Box寸法、Sphere半径、Sphere OffsetのWorld軸加算、Capsule、StaticMesh Geometryの既存契約を維持する。
  親ScaleをColliderパラメータへ追加で乗算しない。
- SoftBody Particleは引き続きSoft local。World Sphere→Soft localの既存一様Scale変換を使用する。
- 非有限/特異/Shear等の直接編集はfalseで拒否する。Scene更新の入力評価に失敗すると当該frameのSimulation / Layerを止め、
  破棄キューだけをflushする。単独Physics入口も入力失敗時に進めない。入力値は自動修復しない。
- `Scene.Transform.SynchronizationFailures` / `Physics.Transform.SynchronizationFailures` は失敗入口の回数。
  `Animation.Transform.RejectedInputs` は検証に失敗したLocal Poseの件数。Profiler有効時のみ記録する。
- 補間用の第2姿勢やrender alphaは導入しない。Physicsは最新の確定Worldを使用する。
- Scene親子はJoint拘束ではない。RagdollPhysicsBridgeのBody/Joint関係、World姿勢転送、衝突除外は変更しない。
- 現在のAnimationSystemは絶対Pose適用であり、Root Motion差分の抽出/消費経路は実装されていない。
  Root Motionを二重適用する新経路は追加せず、既存Character/Locomotionと復帰処理のWorld入力境界を維持する。

## 検証

Debug / Release x64ビルドは終了コード0。通常のPDB生成を有効にした構成は環境由来のMSPDB不一致で失敗したため、
既存の `RavenGenerateLinkDebugInformation=false` とコンパイラ `/Z7` を使用した。Debug CRT / assert / Solver設定は維持した。
checkoutに不足する `glfw3native.h` はGLFW公式3.4版を `artifacts/sg3-deps/GLFW` に取得し、検証コマンドのIncludeだけで補った。
SHA-256: `40035881fdc02a9ab265c2086338f749a0411e1c15b68fada678fa34e8bd8958`。
並列コンパイルは `/MP4`。プロジェクトの依存設定やPDB設定は変更していない。
Debug検証ビルドの警告は既存AstroテストのC4244が9件、コマンド上書きD9025が5件、既存CRT混在LNK4098が1件。
Release検証ビルドにはD9025が5件。最終インクリメンタルでもエラーはなかった。

| CPU検証 | 結果 |
|---|---|
| 独立Storage / TransformMath | 成功、終了コード0 |
| Scene Lifecycle / Scene Transform | 成功、終了コード0 |
| Scene Graph / Editor Entity Command・Inspector | 成功、終了コード0 |
| Transform / PhysicsSimulationWorld / CapsuleCollision / StaticMeshBVH / SoftBodyIntegratedStep | 個別グループ全成功、終了コード0 |
| RagdollRecovery / CharacterCapsule / CharacterLocomotion / BlendTree | 個別グループ全成功、終了コード0 |
| MovingPlatform | 追加修正後に全成功、終了コード0 |
| PhysicsCollision | 追加修正後に全成功、終了コード0 |
| test-windows-scene.ps1全体 | Storage / TransformMath / Lifecycle / Transform / Simulation / PhysicsCollision / Graph / Editor / 計測が全成功、終了コード0 |

初回はSG3 Simulationの10グループ中9グループ成功、1グループ失敗だった。追加修正後は10グループ全成功。Fixed Stepの0/複数回実行・catch-up上限・
Profiler実行回数/破棄時間の既存テストと、新しい同期失敗Counterの照合は成功した。
SceneGraphのwide/chain計測は実行成功。ただしSG3 Local大量負荷の変更前後時間比較を意味しない。

### 初回対応での既存テスト失敗の切り分け

SG3で従来未接続の物理/CharacterテストをCLIへ接続したことで、既存期待値の問題も検出した。
OBB RayCastは元の長いRayがfraction約2.51033で実際にBoxへ当たる。変更していないOBB/AABBを使う独立再現で確認し、
AABBだけに当たる長さ2のRayへテスト入力を修正した。Contact Anchorの収束検証も、補正前の診断値ではなく
出力姿勢から再構成した補正後の固定アンカー誤差を測るよう修正した。初回時点ではSolver本体は変更しなかった。

残る2件は比較用実行ファイルでも再現した。基準 `391a7e02` のPhysicsWorld.cpp、
CharacterControllerMovingPlatform.cpp、元のCharacterMovingPlatformSelfTests.cppをartifactsへ取得・別Objectへコンパイルし、
それら3 Objectを差し替えてリンクした。Sceneや他のObjectは現在版を使用する比較であり、リポジトリ全体の基準版ビルドではない。

- 基準版MovingPlatformテストの `characterTransform.Position.x < 0.03f` が同じ条件で失敗（基準ファイル296行）。
- 基準版PhysicsWorldを用いても `warm.TopHeight > 6.5f && warm.TopHeight < 8.5f` が失敗。

ログ: `artifacts/logs/sg3-moving-platform-baseline-results.log`、`sg3-collision-baseline-results.log`。
これらLegacyケースはLocal Transformを使用しない。初回はSG3同期の有無の切り分けまでで停止した。
その後、ユーザーがこの2件の調査・対応を明示依頼したため、以下の追加修正を行った。

### 追加依頼によるCrush / 積み上げの修正

**移動床Crush:** テストは「壁の左面が既定Capsule Radius + Skin = 0.37、移動余地がない」入力を意図していたが、
実際の左面は0.75 - 0.25 = 0.50であり、0.13移動できた。壁中心を既定CharacterControllerConfigのRadius / Skinと
壁半幅から構成し、意図した左面へ配置した。Characterの衝突/Crush処理と0.03の期待値は変更しない。

**Box SAT軸選択:** 面とほぼ同じ貫通量の交差軸が丸め誤差で選ばれ、4点の面支持が1点の辺支持へ切り替わっていた。
全15軸の分離判定を維持したまま、Edgeへの切り替えは面より5% + 1e-4 World単位以上浅い場合に限定した。
新しい近平行面接触テスト、非交差テスト、真正の斜交Edge接触テストを追加した。
既定8反復の8段・10秒積み上げで、Warm Start時の最上段高さは約7.485、最大貫通は約0.01161を確認した。

**摩擦の蓄積とWarm Start:** 接線が反転/回転した際に、古いスカラーを新しい接線へ直接移していたため、
保存したWorld摩擦Impulseと実際に適用した運動量差が一致しなかった。Worldベクトルとして候補を更新し、
摩擦円錐で制限して差分だけを適用する。保存には既存の接線 + スカラーを使い、Component型や所有権は増やさない。
Warm Start前に摩擦を現在の接平面へ射影し、法線成分を混ぜない。永続ContactのA/B交換時も接線だけを反転し、
接線とスカラーの二重反転を修正した。法線支持・摩擦の蓄積値は引き続きPhysics側が所有する。

**反復予算とテストの役割:** 従来は1速度反復で8段を10秒維持することまで要求していたが、
現在の逐次Solverはその予算で8段の収束を保証しない。長時間Warm / Cold比較をEngine既定の8速度反復へ合わせた。
高さ6.5〜8.5と貫通の合格基準は維持する。1反復のテストは単点で滑り方向を反転/回転させ、
2 Step続けて速度と保存World摩擦Impulseが運動量保存と一致することを独立に検証する。
1反復での高い塔の安定維持を新たに保証したものではない。

**修正前実装による再現確認:** 現在の回帰テストへ、基準版ContactSolver.cppまたはBoxCollision.cppのObjectだけを
それぞれ差し替えた比較実行ファイルを作成した。旧ContactSolverでは新しい単点摩擦の保存Impulse検証が失敗し、
旧BoxCollisionでは既定8反復に直した後も積み上げ高さ検証が失敗した（両方終了コード3）。
修正版はPhysicsCollision全体で終了コード0。テスト条件だけの変更では両方を成功させられないことを確認した。
ログは `artifacts/logs/sg3-old-ContactSolver-results.log`、`sg3-old-BoxCollision-results.log`。

最終の全CPU回帰ログは `artifacts/logs/sg3-followup-full-regression.log`。
Debug / Releaseは既存のPDB回避オプションを用いたビルドが両方成功（終了コード0）。
追加修正のコンパイラ警告はPDB指定上書きのD9025のみで、新規のC++警告はなかった。
実機GPUと長時間実負荷の未検証状態は変わらない。

追加したCPU回帰は、0/複数Fixed Step、親移動時のDynamic保持、SleepingとTeleport、
Kinematicの速度/親回転、Dynamic同士の階層と親破棄、Scale拒否と追加失敗の巻戻し、
Local AnimationとBody Type切替、Sphere/Box/Capsule/StaticMeshのLegacy対比、
親付き移動床、Ragdoll復帰World→Local、Local Rigid/Soft反作用を含む。

`--test-scene-simulation` はWindow/GPUなしで、Transformと既存Capsule /
StaticMesh BVH / PhysicsSimulationWorld / SoftBody / Character Capsule / Moving Platform /
Ragdoll Recovery / Locomotion / BlendTreeの回帰をまとめて実行するDebug専用入口。
`scripts/test-windows-scene.ps1`にも登録する。
グループ単独実行は `--test-scene-simulation MovingPlatform` 等を指定する。
Physics Collision全体は別の `--test-physics-collision` で実行する。
失敗時にWindowsのassert/障害報告ダイアログで待機しないよう、新しいテストCLIだけstderrと非ゼロ終了へ設定する。

Root/Root/Raven配下のファイル構成は変更していないため、structure.txt / vcxproj.filtersの追加登録は不要。
ビルド用の補助ヘッダーとログは追跡対象外のartifacts配下に置く。

## 残る確認

実機GPUの描画/Camera比較、Physics / Animation Demoの目視回帰、長時間実行のLeak、
実負荷でのProfiler時間比較は未検証。非一様Scale剛体、Root Motion機能追加、補間は未実装の制約として残す。
SG4は本マイルストーンの確認と別途承認後に検討する。
