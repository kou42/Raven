# Raven Runtime 実装ロードマップ

## 目的

RE:Runtimeで紹介されている大量インスタンスの集約・一括処理という考え方を参考に、Ravenのゲーム世界を効率よく管理するData-Oriented Runtimeを段階的に構築する。

**既存のRenderer、RHI、PhysicsWorld、Animation内部を一度にECS化しない。** RuntimeWorldから各Subsystem向けのデータを抽出し、Subsystem固有の最適化を維持する。

> この文書はRaven向けの設計提案・実装計画であり、RE ENGINE内部の正式仕様を再現するものではない。記載機能は未実装の計画項目である。

## アーキテクチャ

```text
Scene / Editor / Gameplay
          |
     RuntimeWorld
 Entity / Component / Archetype / Chunk / Query / System
          |
   +------+------+------+
   |             |      |
   v             v      v
PhysicsWorld  RenderWorld Animation
   |             |
Physics       Renderer
                 |
                RHI
```

- Entityはindex + generationの軽量Handleとする。
- Componentは基本的にデータを保持し、Systemが処理を担う。
- 同じComponent構成のEntityをArchetypeでまとめ、固定容量Chunk内でSoA配置する。
- Queryは該当Archetype/Chunkを列挙し、Job Systemによる並列処理へ拡張する。
- Sceneの編集表現とRuntimeの実行表現を混同しない。
- Physics Solver、RHI、Renderer内部のデータ構造は独立させる。
- Componentの構築・破棄・移動、アラインメント、例外安全性、Entity Handle無効化を明示的に扱う。

## Phase 01 — Entity Handle / Lifetime

- [ ] 既存Scene/GameObject/Component管理の責務と利用箇所を調査する
- [ ] `Entity { index, generation }` と無効値を定義する
- [ ] Entity ID再利用時のgeneration更新を実装する
- [ ] Create/Destroy/IsAliveを実装する
- [ ] Handleの寿命・World間混用防止方針を決める
- [ ] 破棄後Handle、二重破棄、ID再利用をテストする

**完了条件:** stale handleを安全に検出できる。

## Phase 02 — Component Type Registry

- [ ] Component Type IDの生成・安定性を設計する
- [ ] サイズ、アラインメント、構築・移動・破棄関数のメタデータを保持する
- [ ] 非trivial Componentの扱いと禁止条件を明示する
- [ ] Type RegistryとComponent登録APIを実装する
- [ ] 重複登録と型不一致を検出する

**完了条件:** Componentの型情報を安全に取得できる。

## Phase 03 — Archetype

- [ ] Component Type ID集合を正規化したArchetype Keyを定義する
- [ ] Archetype Registryと検索を実装する
- [ ] Entity追加先のArchetypeを決定する
- [ ] Archetype間遷移の設計を決める

**完了条件:** 同一Component構成のEntityが同じArchetypeに所属する。

## Phase 04 — Chunk / SoA Storage

- [ ] 16KiBを初期候補とし、計測可能なChunk容量を定義する
- [ ] Component列のOffset、Alignment、Capacityを計算する
- [ ] SoA StorageとChunkの確保・解放を実装する
- [ ] Entity → Archetype / Chunk / Row lookupを実装する
- [ ] 行削除時のswap-removeと移動先lookup更新を実装する
- [ ] Over-aligned / 大型Componentと空き容量をテストする

**完了条件:** 複数ChunkへEntityを格納し、移動・削除後もlookupが正しい。

## Phase 05 — Structural Changes

- [ ] AddComponent / RemoveComponentを実装する
- [ ] Archetype移行時に共通Componentを安全に移動する
- [ ] 移行前後の構築・破棄・失敗時処理を検証する
- [ ] Query走査中の構造変更を禁止またはCommand Bufferへ遅延する
- [ ] Deferred Structural Command Bufferを必要に応じて導入する

**完了条件:** Component追加・削除とEntity破棄を繰り返してもデータが破損しない。

## Phase 06 — Query

- [ ] `Query<Transform, Velocity>()` 相当のAPIを設計する
- [ ] 必須Component集合でArchetypeをフィルタする
- [ ] Chunk単位で型付きComponent列を列挙する
- [ ] 読み書きアクセスの契約とIterator無効化条件を明示する
- [ ] Queryキャッシュの更新条件を設計する

**完了条件:** 一致するEntityだけをChunk単位で処理できる。

## Phase 07 — System / Scheduler

- [ ] System登録・実行順序を定義する
- [ ] Read/Write Component Accessを宣言できるようにする
- [ ] 依存関係に従うSystem実行順を検証する
- [ ] Fixed UpdateとVariable Updateを分離する
- [ ] Chunk単位Job分割と同期Barrierを導入する
- [ ] Race、決定性、過小Jobのオーバーヘッドを計測する

**完了条件:** Movement Systemを安全にChunk並列実行できる。

**Milestone R1 — Runtime Core v1（Phase 01〜07）**

## Phase 08 — Diagnostics Integration

- [ ] Entity / Archetype / Chunk数のCounter
- [ ] Chunk Utilizationと構造変更回数のCounter
- [ ] System / Query / ExtractionのTrace Scope
- [ ] Debug/Releaseで計測オーバーヘッドを比較する

**完了条件:** Runtimeの処理時間とメモリ利用状況を追跡できる。

## Phase 09 — RenderWorld Extraction

- [ ] Transform + MeshRenderer Queryを設計する
- [ ] RenderInstanceなど描画用Snapshotを抽出する
- [ ] RuntimeWorldとRenderWorldの所有権・更新タイミングを決める
- [ ] Visibility / Render Queue / RHIとの境界を確認する
- [ ] Render結果と既存Scene経路の一致を検証する

**完了条件:** RendererがRuntime Componentを直接参照せず描画できる。

## Phase 10 — PhysicsWorld Integration

- [ ] EntityとPhysics Body Handleの対応表を設計する
- [ ] Collider/RigidBody変更のPhysicsWorldへの反映を定義する
- [ ] Fixed Step前のExtractとStep後のTransform Writebackを実装する
- [ ] Transform所有権、Kinematic/Dynamic、補間、親子関係を整理する
- [ ] BroadPhase / NarrowPhase / Solver / Joint / SoftBodyは既存設計を維持する
- [ ] 生成・破棄・衝突・Joint・Ragdollの回帰テストを行う

**完了条件:** Runtime EntityとPhysicsWorldを同期して既存Physicsが動作する。

## Phase 11 — Animation Integration

- [ ] Animator/Animation StateのComponent表現を検討する
- [ ] Clip / BlendTreeのバッチ評価可能性を計測する
- [ ] Skeleton PoseとTransform更新の所有権を決める
- [ ] Root Motion、Character Controller、Ragdoll連携を検証する

**完了条件:** Animation更新がRuntimeと同期し、既存キャラクター動作を維持できる。

## Phase 12 — Editor / Asset / Hot Reload

- [ ] Runtime Entity InspectorとArchetype/Chunk可視化
- [ ] Scene SerializationとRuntime Component変換
- [ ] Asset変更からRuntime Handle更新への伝播
- [ ] Followsによる依存関係・Invalidation追跡
- [ ] Play Mode開始・停止とWorld再生成の寿命管理

**完了条件:** EditorからRuntime状態を調査でき、Asset変更が適切に反映される。

## Phase 13 — Optional Managed Layer

C#、Visual Scripting、Code GenerationはRuntime Core v1の必須条件にしない。

- [ ] C++/C#境界とABI、Handle、例外・GC Lifetimeを検討する
- [ ] C# Component Metadata / Source Generatorを検討する
- [ ] Inspector / Serialization / Visual Scriptとの型共有を検討する
- [ ] Native実行との性能・保守コストを比較して採否を決める

## APIイメージ（未確定）

```cpp
struct Entity
{
    uint32_t index;
    uint32_t generation;
};

struct Velocity
{
    Vec3 linear;
    Vec3 angular;
};

// 概念例。実際のWorld/Query APIは既存Raven規約に合わせる。
for (auto chunk : world.Query<Transform, Velocity>())
{
    auto transforms = chunk.Get<Transform>();
    auto velocities = chunk.Get<Velocity>();
    for (size_t i = 0; i < chunk.Size(); ++i)
    {
        transforms[i].position += velocities[i].linear * dt;
    }
}
```

## マイルストーンと検証

| Milestone | Phase | 成果物 | 重点検証 |
| --- | --- | --- | --- |
| R1 | 01〜07 | Runtime Core v1 | Handle・構造変更・Query・並列実行 |
| R2 | 08 | Runtime Profiling | Counter精度・オーバーヘッド |
| R3 | 09 | RenderWorld | Scene描画の回帰 |
| R4 | 10 | PhysicsWorld | Fixed Step・Transform同期 |
| R5 | 11 | Animation | Root Motion・Character |
| R6 | 12 | Editor/Hot Reload | Lifetime・再構築 |
| R7 | 13 | Managed Layer（任意） | ABI・GC・性能 |

## 推奨実装順

1. 既存Scene/Componentの構造を調査し、RuntimeWorldの責務境界を決める。
2. Phase 01〜07で独立した小さなRuntime Core v1を完成させる。
3. Diagnosticsと統合してEntity/Chunk/Systemの性能を可視化する。
4. RenderWorld Extractionを先行し、既存Rendererを維持したまま接続する。
5. PhysicsWorld、Animation、Editorへ段階的に展開する。
6. C#やVisual Scriptingは必要性と計測結果に基づいて判断する。

## 実装時の注意

- `Root/Root/Raven` 配下のファイル構成を変更する際は `structure.txt` を更新する。
- Visual Studio管理ファイル追加時は対応する `.vcxproj.filters` も更新する。
- 既存APIと重複する新規実装を避けるため、実装開始時に `AGENTS.md`、`structure.txt`、関連ソースを確認する。
- 各PhaseでDebug/Releaseのビルドと関連テストを行い、未実施の検証を明記する。
