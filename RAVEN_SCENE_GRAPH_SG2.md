# Scene Graph SG2 — Local / World Transform

基準master: `cc44737927dde7497983d966e929865098030738`（SG1統合済み）。
SG2の実装とWindows CPU回帰テストを完了。実機描画の比較は未検証。SG3には進まない。

## 互換経路と所有権

既存Transform参照は基準時点で105ファイルにあり、Scene/Editor/Physics/Animation/Gltf/Rendererに分布する。
既存のTransformComponent::Position/Rotation/ScaleとGetTransformはWorld-spaceの窓口として維持する。
LocalTransformComponentを持たないEntityは、Parentがあっても従来どおりWorld-spaceで独立する。
Localを有効にしたEntityだけ、その親のWorld × 自身のLocalで評価する。Legacyの親はWorldの基準となる。
既存Asset/Demoを暗黙に切り替えず、テスト用EntityとCameraのWorld→Local移行から互換性を確認する。

LocalTransformComponent::ValueはLocal authoring。SceneTransformSystemはSceneが所有するWorldキャッシュで、
Component参照/pointerやScene pointerを保持せず、完全なEntityHandle（Generation込み）で索引する。
HandleはScene内識別子であり、Handle APIの呼び出し側がScene所属を保証する。
キャッシュは派生データなのでEditor Snapshotに保存しない。Entity破棄時にForget、Scene終了時にClearする。
SceneGraphは階層の妥当性・親変更を担当し、幾何検証をTransform Systemへ委譲する。

## APIと評価

- EnableLocalTransform: 既定はKeepWorld。KeepLocalなら既存TRSをLocalの初期値として扱う。
- DisableLocalTransform: 現在のWorldをLegacy経路へ引き継ぐ。
- SetLocalTransform/SetWorldTransform: 対象と子孫を検証し、不成立時は入力・親を変更しない。
- SetEntityParent(child, parent, mode): KeepWorld/KeepLocalを明示。既存2引数版はKeepWorld。
- SetEntityParentAndLocal: Editorが親と保存済みLocalを同時に復元する、検証付き操作。
- EvaluateWorldTransforms: 共有する親を一度だけ解決し、非再帰で親→子順に評価する。
- TryGetEntityWorldMatrix: 公開TRSへの直接書き込みも検出し、その時点のWorldを解決する。
- TryGetEvaluatedWorldMatrix: 評価直後の変更のない描画バッチ専用。Scene/SceneGame/Pickingはバッチ開始時に評価する。

Scene更新の最後（Layerと破棄Queue後）にWorldを評価する。Pause経路でも評価する。
Camera/Outline/Inspectorは必要に応じて現在のWorldを解決し、描画とCameraは評価後のWorld入力を使う。
従来の60Hz固定Step・catch-up上限・Animation→Physics→Layerの順序とSubsystem内部は変更しない。

## Dirtyと直接編集

Local/World互換TRSの直前値、親Handle、親World Matrixの変化からDirtyを検出する。
再計算しなくてよい場合は前回のWorld Matrixを再利用し、親が変われば子へ伝播する。
GetWorldTransformRecomputationCountは、採用されたWorld Matrix再計算の累積数を返す。
検証だけで破棄された候補や、全体走査・フィールド比較のCPUコストはこのCounterには含めない。

既存Inspector/GizmoのWorld編集はParent逆行列からLocalへ戻す。
LocalとWorldの両方を同じ評価までに直接編集した場合は意図が不明なので評価を拒否する。
SetLocalTransform/SetWorldTransformで、どちらを正規入力とするか明示して修復できる。
直接編集した値を直ちに読む場合はEvaluateまたは現在World取得APIを呼ぶ。単なるComponent参照取得では評価しない。

公開フィールドへの不正な直接編集自体を巻き戻すことはしない。評価失敗はfalseで返し、
描画・Cameraへ古いキャッシュや不正なMatrixを送らない。既存InspectorのPreviewとGizmoの確定は検証APIを通し、
不成立の変更を履歴へ確定しない。新しいLocal/World切替Widgetは追加せず、導入は明示APIで行う。

## 行列・失敗の契約

row-major格納、列Vector、Local = T * Rx * Ry * Rz * S、World = ParentWorld * Local。
角度はradian。既存Quaternion::FromEulerXYZ/ToEulerXYZのRz*Ry*Rxと異なるため、TRS分解はSceneの順序で行う。
アフィン逆行列とTRS分解はchecked APIとし、失敗時に出力を変更しない。

LocalのWorldもTRSで表せる範囲だけをSG2で受け付ける。
keep-world/World設定/Local解除では、再構成Matrixが要求Worldに対して各成分1e-4 × (1 + |要求値|)以内かも検証する。
有限の逆行列でも、巨大な親座標との丸めで要求World位置を保持できなければ変更前に拒否する。非一様Scale×回転で生じるShear、
ゼロ/極小Scale、非有限値、逆行列不能を拒否する。基底の直交/相対特異性判定の許容値は1e-4、
TRS各軸Scaleの最小絶対値は1e-6。負の行列式はX軸の負Scaleへ正規化する。
Euler角・Scale符号の表現が変わっても再構成したMatrixが同じになる契約とする。
Cameraは従来どおりWorld互換TRSのScaleを無視し、正規化されたWorld回転と位置を使用する。
同じMatrixでも反射Scaleの符号正規化でCamera回転が変わる場合があるため、keep-worldとWorld編集では
Scaleを除いた回転Matrixも照合する。Viewの向きを保持できない候補は変更前に拒否する。
KeepLocal/Local編集は新しいWorld姿勢を指定する操作であり、符号正規化されたWorld回転を使用する。

親変更/Transform変更で子孫にShearが生じる場合も、全候補を検証してから変更する。
失敗時は親・Local/Worldの正規入力を変更しない（メモリ確保失敗を跨ぐ一般的なECSトランザクションではない）。
Localが依存する親経路の循環・破棄済みHandle・Transform欠落も拒否し、深い階層で再帰stackを使わない。

## 破棄とEditor履歴

親破棄前に直下のLocal子のWorldを解決し、新しいRootのLocalへ引き継ぐ。孫の関係は保つ。
壊れた直接編集で解決できなければ最後の有効World（未評価なら互換TRS）を引き継ぐ。
Localを保持したままTransformを削除することや、Local子が依存する親Transformの削除は拒否する。
Local Componentの通常削除はDisable経路、Relationshipの削除はKeepWorld Detach経路を通す。

Reparent Commandは親Handleに加えて変更前後のLocalを保存し、Undo/Redoで原子的に復元する。
Transform CommandはWorld編集として保存し、適用時にLocalへ変換する。
Delete Commandは自身と直下のLocal子をSnapshotし、再生成された親HandleへLocalを再接続する。
保存した親が消えていれば削除前WorldをRootのLocalにする。別の親へ移動した子を奪い返さない。
全ての復元候補を先に幾何検証し、部分的な子の復元状態で失敗しないようにする。

## SG3との境界

Local Entity自身のRigidBody/Collider/Animator/MeshDeformationとの併用をSG2では拒否する。
Enableはfalse、相反するComponentの直接追加はinvalid_argumentを返す。既存EntityはLegacyのままなので影響しない。
Local描画子がLegacyの動く親を追うことはできるが、Local EntityをPhysics Domainの姿勢入力・出力先として
接続することや、固定Step内のLocal/World同期はSG3の対象であり未対応。

## 検証

- 独立TransformMathTests: 正/負Scale、3軸回転、ジンバルロック、逆行列両側積、Shear/特異/非有限/非アフィンの拒否、失敗時出力保持。
- SceneTransform SelfTest: opt-in/out、親子伝播、Dirty/再計算省略、直接編集、両モード、子孫Shearの変更前拒否、世代再利用、親破棄、512段の階層。
- Camera: Legacy→Local→LegacyのWorld/View/Projection比較。
- Editor: Local付きReparent/World編集のUndo/Redo、親削除復元、失われた親、選択/Inspector Snapshot。
- Linuxでは独立数値テストをASan/UBSanで実行（ptrace環境のためLeakSanitizer無効）。関連翻訳単位をGCC C++17で構文確認する。
- Windows Debug/Releaseビルドと既存Storage/Scene Lifecycle/SceneGraph/Editor回帰に、新しい数値・Transformテストを追加する。
- 実機GPU描画・Physics/Animationシミュレーションの目視回帰とLeak検証は未実施として残す。

### Windows CI結果

実装コミット `10525d4b2a8949768668294933d3183b0beba27a` の
[Windows Build #51](https://github.com/kou42/Raven/actions/runs/42045612142) は全4ジョブ成功。
初回はGLFW取得時のHTTP 504で3ジョブが停止したため、失敗ジョブを再実行した。

- Debug/Release x64エンジンビルド、Storage/TransformMath/Scene Lifecycle/SceneGraph/SceneTransform/Editor回帰を実行し成功。
- 巨大親座標でWorldを保持できない変更の拒否、反射Scale付きCameraの向き保持も回帰テストで確認。
- 既存Debug/Release CPU UI回帰とUI allocation profilingが成功。
- GPU integration test実行ファイルのビルドが成功。実機GPU上での実行成功を意味しない。

この結果追記は文書のみの変更であり、上記検証対象の実装・テスト・CI設定は変更していない。
実機描画とPhysics/Animationシミュレーションの目視回帰、Leak検証は未実施。
