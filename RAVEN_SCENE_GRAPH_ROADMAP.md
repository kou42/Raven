# Raven Scene Graph / ECS 段階的改善ロードマップ

> 状態: SG0の安全性修正とWindows Debug/Releaseビルド・CPU回帰テストを確認済み（Editor/Physics/Animation実機回帰は未検証）。詳細は [SG0作業記録](RAVEN_SCENE_GRAPH_SG0.md)。2026-11-04 時点の `master` 上の既存設計を基に作成。各マイルストーンの実装前に `AGENTS.md` の承認ゲートに従う。

## 目的と結論

既存の **EntityHandle（Index + Generation）/ Scene / Sparse Set ComponentStorage / ComponentView** を維持し、Scene Graph と Transform の責務を段階的に強化する。既存 PhysicsSimulationWorld、AnimationSystem、Renderer/RHI の内部は一括変更しない。

**採用方針:** 既存 Sparse Set ECS + 独立した Scene Graph + 計測で必要性を確認した場合のみ Archetype/Chunk RuntimeWorld。

**非目標:** Actor/GameObject継承階層への置換、既存ECSの再実装、UE6内部仕様の再現、初期段階での汎用Scheduler導入。

## 既存実装の確認

- `Root/Root/Raven/Scene/Entity.h`: `EntityHandle` は Index/Generation を保持。`Entity` は Scene pointer を持つ薄い操作ラッパー。
- `Scene/Scene.h`, `Scene/Scene.cpp`: Entity slot/free list、生成・破棄・生存確認、ComponentStorageの所有、親設定、固定Physics Step、Animation→Physics→Layerの更新順序。
- `Scene/ComponentStorage.h`: `vector<T>` + Entity配列 + Sparse配列、swap-and-popによる削除。
- `Scene/ComponentView.h`: 複数Componentを照合するView。
- `Scene/Components.h`: `RelationshipComponent` は親Handleのみ。`TransformComponent` は現在World-spaceのPosition/Rotation/Scaleであり、親子Transform伝播は未実装。
- `Scene/SceneCameraSystem.h`: Camera設定とTransformをSystemで結合する責務分離の先例。
- `RAVEN_RUNTIME_ROADMAP.md`: 別途、Archetype/Chunk型RuntimeWorldの長期構想あり。**本ロードマップはそれを置き換えない。**

## 不変条件

1. 破棄済みHandleやGeneration不一致を誤って有効化しない。Sceneを跨ぐEntity操作はScene所属を検証する。
2. Component追加・削除・View走査の安全性を保つ。構造変更による参照・Iterator無効化条件を文書化する。
3. 親子階層は循環不可。親の削除・Index再利用・Undo/Redo時に不正参照を残さない。
4. 初期移行中は既存World-space描画・物理挙動を変えない。Local/World変換は明示的に切り替える。
5. PhysicsのDynamic BodyはPhysics側がWorld姿勢の正規データ、KinematicはGameplay/Animation側が入力を与える。RagdollやJointの階層はScene親子階層と同一視しない。
6. 既存の60Hz固定Step、catch-up上限、Profiler、RHIリソース準備経路を維持する。
7. `Root/Root/Raven` 配下にファイルを追加・移動したら `structure.txt` を更新。Visual Studio管理ファイルを追加したら `.vcxproj.filters` も更新。

## Milestone SG0 — ベースラインと安全性（最優先）

**対象候補:** `Scene/Entity.h`、`Scene/Scene.h/.cpp`、`Scene/ComponentStorage.h`、`Scene/ComponentView.h`、関連テスト。

- [x] `structure.txt` と利用箇所を再調査し、Scene所有権・Entity破棄・Component Viewの無効化規則を一覧化。
- [x] `Scene::IsEntityAlive(Entity)` 等のScene所属判定を監査。異なるSceneで同じIndex/Generationを持つEntityを拒否する契約をテスト。
- [x] IndexベースのComponent APIに対する生存確認の境界を決定。内部高速経路と公開安全APIを区別。
- [x] Sparse Setの追加・削除・移動時の例外安全性、非trivial型、参照無効化を確認し、必要最小限の修正。
- [x] Entity再利用、二重破棄、破棄キュー、View走査中の変更、親Handle再利用を回帰テスト。

**完了条件:** 不正Scene/古いHandleが誤ったEntity/Componentへ到達しないことをテストで確認。既存動作とビルドを維持。

## Milestone SG1 — Scene Graphの責務分離

**対象候補:** `Scene/Scene.h/.cpp`、`Scene/Components.h`、新規 `SceneGraph`（必要な場合のみ）、Editor階層表示・テスト。

- [ ] `RelationshipComponent::Parent` と `SetEntityParent` を正規APIとして維持し、階層管理の責務を抽出。
- [ ] Reparent、Detach、祖先・子孫走査、循環防止、親破棄時の子の扱いを明文化。
- [ ] 子一覧を毎回走査する方式と子リンク/キャッシュ方式を計測し、必要な場合だけ高速化。
- [ ] Editor SceneHierarchy、選択、Undo/Redo、Snapshotとの整合性を検証。

**完了条件:** 親子構造の変更・破棄・復元が正しく、既存World-space Transformの描画/物理結果が変わらない。

## Milestone SG2 — Local / World Transform導入

**対象候補:** `Scene/Components.h`、Transform計算、SceneGraph、Camera/Renderer/PhysicsのTransform利用箇所。

- [ ] 既存 `TransformComponent::Position/Rotation/Scale` の意味と全参照箇所を調査。互換APIと移行期間を決定。
- [ ] Local Transform、World Matrix、Dirty伝播を導入。行列の積順、座標系、Quaternion/Euler変換を明記。
- [ ] 親変更時のkeep-world/keep-localを明示。非一様スケール×回転によるShearはTRS分解失敗またはMatrix保持の契約を定める。
- [ ] Transform評価を親→子順に実行し、循環・深い階層・大量子Entityを検証。
- [ ] World-spaceの既存Entityを段階的に移行し、Camera/Rendererの出力を比較。

**完了条件:** 階層移動/回転/スケールが正しく伝播し、非階層Entityの描画とカメラが回帰しない。

## Milestone SG3 — Physics / Animation Transform同期

**対象候補:** `Scene/Scene.cpp`、`Physics/PhysicsSimulationWorld`、`Animation/AnimationSystem`、関連Component/System。

- [ ] Static/Kinematic/Dynamic別のTransform書込権限と同期タイミングを定義。
- [ ] Physics Fixed Step前のKinematic入力とStep後のDynamic World姿勢書戻しを検証。
- [ ] 親ありDynamic BodyのWorld→Local変換、親の非一様スケール、補間の制約を明記。
- [ ] Animation Root Motion、Character Controller、Ragdoll復帰、JointとEntity親子の独立性を検証。
- [ ] Sphere/Box/Capsule/StaticMesh Collider、SoftBody/Rigid Couplingの既存座標契約を維持。

**完了条件:** 物理・アニメーション・親子Transformが競合せず、既存デモの挙動とProfiler指標に重大な回帰がない。

## Milestone SG4 — Component View / System実行の計測改善

- [ ] `ComponentView` の候補数、走査時間、不要な照合、構造変更時の安全性を計測。
- [ ] 既存 `Scene::OnUpdate` の順序を基準に、明示フェーズ・同期Barrierが必要か検証。
- [ ] 必要な場合だけ最小限のScheduler/Command Bufferを導入。汎用並列Schedulerは先行導入しない。
- [ ] Debug/Releaseで性能・メモリ・破棄コストを比較。

**完了条件:** 変更前後の計測値を記録し、改善が実証された最適化だけを採用。

## Milestone SG5 — RuntimeWorldとの接続判断

`RAVEN_RUNTIME_ROADMAP.md` のArchetype/Chunk構想と統合して判断する。

- [ ] Sceneを編集/Gameplayの正規データとするか、RuntimeWorldを正規データとするか、対象Component単位で所有権を決定。
- [ ] 代表的な大量Entity負荷でSparse SetとArchetype/Chunkを比較。Query/移動/生成破棄/同期/メモリを測定。
- [ ] RenderWorld向けSnapshot抽出など、効果が高い経路だけ段階移行。
- [ ] 二重保持するデータは同期方向・更新タイミング・破棄通知を明示。

**完了条件:** Archetype採否が計測結果と所有権設計に基づいて決定され、既存Subsystemを一括ECS化しない。

## 実装・検証の運用

- 各SGマイルストーンの開始前に目的、変更予定ファイル、ビルド/テスト、完了条件、リスクを提示し、ユーザーの明示承認を得る。
- 各段階でDebug/Releaseビルド、関連単体テスト、Editor/Physics/Animation回帰確認を行う。実行不可の場合は未検証と明記。
- マイルストーンごとに独立した差分と日本語コミットメッセージを使い、完了後は次のマイルストーンへ自動進行しない。
- 既存 `RAVEN_RUNTIME_ROADMAP.md` のPhase 01〜07は「既存SceneのEntity/Sparse Setを再実装する」という意味にはしない。独立RuntimeWorldの新規要件に限って着手し、重複実装を避ける。

## 推奨着手順

**SG0 → SG1 → SG2 → SG3 → SG4 → SG5**。

最初にSG0で安全性と回帰テストの基盤を確立する。Scene Graph自体の分離はSG1、既存のworld-space互換性に最も影響するTransform階層化はSG2で扱う。
