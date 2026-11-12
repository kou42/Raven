# Raven Runtime 実装ロードマップ

## 目的と現在の判断

Ravenの既存Scene / Component Storage / Physics / Animation / Rendererを正規データ所有者として維持し、安全性・診断性・処理効率を計測に基づいて改善する。Data-Oriented設計やArchetype / Chunkは将来の選択肢として保持するが、独立RuntimeWorldの構築を現在の実装目標とはしない。

**SG5の接続判断:** SceneのSparse Setを維持する。固定型Chunk mirrorは単体kernelでは高速だったが、毎FrameのScene全量入力同期・書戻しを含むRelease比較で約3倍の処理時間となった。これは今回のmirror方式に対する判断であり、Archetype一般や独立所有型Runtimeの否定ではない。独立RuntimeWorld、汎用Archetype / Chunk / Query / Schedulerは、新たな要件と実測上の根拠が揃うまで実装を保留する。

SG5の所有権、測定条件、未検証項目は[Scene Graph SG5](../close/RAVEN_SCENE_GRAPH_SG5.md)を参照する。

## 現行アーキテクチャ（維持）

```text
Scene / Editor / Gameplay
  ├─ EntityHandle (Index + Generation) / Component Storage / Scene View
  ├─ SceneTransformSystem / Animation
  ├─ PhysicsSimulationWorld → PhysicsWorld
  └─ Render Queue → RHISceneMesh → RHISceneDrawItem Snapshot
                                     ↓
                                  Renderer → RHI
```

- Entity Index / Generation、Tag、Relationship、Component所属はSceneが正規所有する。
- Local Transformと階層評価はScene / SceneTransformSystem、Dynamic姿勢・Solver内部はPhysicsが所有する。
- Animationは既存Scene更新順を維持する。Root Motionの新規抽出・消費は未実装。
- Render SnapshotはSceneからFrameへの一方向抽出であり、Scene/Component pointerを保持しない。GPU Resourceは共有参照とする。
- SG5で追加された`RHISceneDrawItemBuilder::BuildInto`は出力vector再利用用のAPIであり、RenderWorldやFrame poolの導入ではない。

## 将来構想（採用未決定）

```text
Scene / Editor / Gameplay
         ↓  （所有権・変換コストを別途設計）
独立RuntimeWorld（Archetype / Chunk / Query / Scheduler）
         ↓
Subsystem向け抽出 → Physics / Renderer / Animation
```

この図は将来の研究対象であり、現在の実装構成や実装承認を表さない。毎Frame全量mirrorはSG5で不採用。独立所有型Runtimeやdirty差分同期は未測定。

## 現在の改善計画

### Phase 01 — Existing Scene Entity Lifetime Audit / Hardening

- [x] 既存`EntityHandle`、`Entity`、`Scene::CreateEntity` / `DestroyEntity` / `IsEntityAlive`、Free List、Generationの契約を文書化する。
- [ ] stale handle、二重破棄、Index再利用、別SceneからのEntity破棄拒否について既存テストの範囲を確認し、不足するケースのみ追加する。（既存範囲の確認とEditor交換回帰の追加済み。Pickingの同一Scene内Index再利用競合は個別設計待ち。）
- [x] `Entity`が非所有の`Scene*`を持つため、Scene破棄後のアクセスを禁止する既存契約と、SceneManager / Editor / 非同期処理側の保持期間を調査する。
- [x] Indexのみを受け取るComponent APIとGeneration付きHandle APIの使用箇所・混用リスクを調査する。
- [x] GPU PickingがIndexのみを渡し、CPUでGenerationを再取得する既存経路のreadback中のIndex再利用を調査する。
- [ ] 実際の不足が判明した場合だけ、既存Sceneの最小修正を別途提案・実装する。新しいEntity allocatorやRuntimeWorldは作らない。

**既存実装の確認（2026-11-13）:** `Scene/Entity.h`にはIndex + Generationの`EntityHandle`とScene所属を保持する`Entity`が存在する。`Scene/Scene.cpp`ではFree List再利用、Destroy時のGeneration更新、`IsEntityAlive(Entity)`でのScene照合、終了時のEntity sweepが実装済み。`Scene/Scene.inl`にはIndexを引数とするComponent APIがある。これらを未実装として重複実装しない。

**完了条件:** 既存寿命契約、テストの保証範囲、残存リスクが明文化され、確認された不具合にのみ回帰テストと修正がある。

**調査記録（2026-11-13）:** [Phase 01寿命調査](RAVEN_RUNTIME_PHASE1_LIFETIME_AUDIT.md)。既存テストの保証範囲を整理し、EditorのScene交換後参照を承認済み範囲で修正した。Pickingの同一Scene内Index再利用、Generation上限等の残存リスクは分離し、Phase 01全体は未完了とする。

### Phase 02 — Scene Structural Safety

- [x] Component追加・削除、Entity破棄、View走査中の構造変更とIterator無効化契約を確認する。
- [x] 既存ComponentStorage / Scene ViewのDebug検証とReleaseでの前提を確認する。
- [x] 必要性が実証された場合のみ遅延Structural Commandを検討する。（既存Queueと更新境界を維持し、今回は新設しない。）

**完了条件:** 既存Sceneの構造変更の安全な利用条件がテスト・文書で確認できる。

**完了記録（2026-11-13）:** [Phase 02構造変更の安全性](RAVEN_RUNTIME_PHASE2_STRUCTURAL_SAFETY.md)。既存実装を維持し、構築失敗・即時破棄後のDebug検出、両構成での安全な再取得と所有資源解放の回帰を追加。Debug / Releaseビルドと関連CPUテスト成功。ローカルビルド指定・未検証事項は記録を参照。Phase 03には自動で進まない。

### Phase 03 — Scene Diagnostics / Performance

- [x] 既存CPUProfiler、Scene View統計、SG5比較を再利用して測定対象を定義する。
- [x] Entity / Component数、Query / 構造変更時間、メモリ、確保回数を必要に応じて測定する。（fixture件数、累積new要求量、mirror/Snapshot保持容量。Scene総保持量は未測定。）
- [x] Debug / Release差と測定オーバーヘッドを記録する。

**完了条件:** 変更の必要性を実測から判断できる。

**完了記録（2026-11-13）:** [Phase 03計測結果](RAVEN_RUNTIME_PHASE3_MEASUREMENTS.md)。統計付き走査の反復中央値、Profiler有効/無効と公開件数照合を追加。通常Debug/Releaseと別Release確保計測で基準値を取得し、全量mirror不採用と既存所有権を維持する判断を記録した。実Demo/GPU・長時間負荷は未検証。Phase 04には自動で進まない。

### Phase 04 — Existing Render Snapshot Validation / Optimization

- [ ] 既存`BuildInto`の利用範囲とFrame/vector寿命を調査する。
- [ ] CPU抽出の時間・確保数と実際のFrame準備・GPU描画を区別して測定する。
- [ ] SG2/SG3/SG5から残る実機GPU、Demo、長時間負荷を優先して検証する。
- [ ] GPU fence、Backend別寿命、失敗時の出力維持を確認せずFrame poolを導入しない。

**完了条件:** Snapshot経路の正確性と改善効果を実測で説明できる。

### Phase 05 — Existing Physics / Animation Synchronization

- [ ] Scene TransformとPhysics Dynamic / Static / Kinematicの所有権、Fixed Step前後の同期を回帰検証する。
- [ ] Animation Pose、Character Controller、Ragdollの既存更新順を維持する。
- [ ] Root Motion抽出・消費は別要件として扱い、RuntimeWorld導入の前提にしない。

**完了条件:** 既存Subsystem間の同期契約と回帰テストが整理される。

### Phase 06 — Runtime Architecture Reassessment（条件付き判断）

- [ ] 大量同型Entityを複数Systemで繰り返し処理する実要件、または既存Sceneの実測ボトルネックを確認する。
- [ ] Sparse Set、必要なら差分同期、独立所有型Chunkを同等の処理範囲・正規所有権条件で比較する。
- [ ] 入力同期、書戻し、構造変更、生成破棄、メモリ、非trivial Component、Subsystem連携の総コストを含める。
- [ ] 結果と移行コストをレビューし、独立Runtimeを別マイルストーンとして承認するか決定する。

**完了条件:** 採用・不採用の判断と測定根拠が記録される。独立RuntimeWorldの実装完了は条件に含まない。

## 保留中の独立Runtime構想（旧Phase 01〜07）

以下は**未承認の将来候補**であり、現在のPhase番号・マイルストーンには含めない。

1. 独立Entity Handle / Lifetime（Sceneとの所有権重複を回避する設計が前提）
2. Component Type Registry（型ID、alignment、非trivial型の構築・移動・破棄）
3. Archetype Key / Registry / 遷移
4. Chunk / SoA Storage（容量は測定で決定し、16KiBを固定仕様としない）
5. Structural Changes / Deferred Command Buffer
6. Chunk Query / キャッシュ / Iterator無効化契約
7. System / Scheduler / Job並列化 / Barrier / Race・決定性検証

旧Phase 08〜13にあったDiagnostics、RenderWorld Extraction、Physics / Animation Integration、Editor / Hot Reload、Optional Managed Layerも、独立Runtimeを前提とした新規実装は保留する。既存Sceneの改善に必要な部分だけ現在のPhase 01〜05で扱う。C# / Visual Scriptingは必須条件ではない。

## マイルストーンと検証

| Milestone | 対象 | 成果物 | 重点検証 |
| --- | --- | --- | --- |
| R1 | Phase 01 | Scene Entity Lifetime調査・不足補完 | stale handle、Scene間混用、破棄後参照、Picking |
| R2 | Phase 02 | Structural Safety | View無効化、構造変更、Component寿命 |
| R3 | Phase 03 | Scene Profiling | Counter精度、CPU・メモリ負荷 |
| R4 | Phase 04 | Render Snapshot検証 | GPU実機、Frame寿命、回帰 |
| R5 | Phase 05 | Physics / Animation同期検証 | Fixed Step、Transform、Character |
| R6 | Phase 06 | 独立Runtime再評価の判断記録 | 総コスト、正規所有権、採否 |

各Phaseは調査・検証の結果、不足がなければコード変更なしで完了できる。

## 推奨実装順

1. SG5で残った実機GPU / Demo / 長時間負荷検証を優先する。
2. Phase 01で既存Scene Entityの寿命契約と不足を調査する。新規Entity実装は行わない。
3. Phase 02〜03で構造変更の安全性と性能の基準値を確認する。
4. Phase 04〜05は既存Render / Physics / Animationの経路を維持して必要な改善だけを行う。
5. Phase 06で新しい実要件・計測結果が揃った場合に限り、独立RuntimeWorldの別計画を承認判断する。

## 実装時の注意

- `Root/Root/Raven`配下のファイル構成を変更する際は`structure.txt`を更新する。
- Visual Studio管理ファイル追加時は対応する`.vcxproj.filters`も更新する。
- 実装開始時に`AGENTS.md`、`structure.txt`、関連ソース・既存テストを確認する。
- 既存Scene / Physics / Animation / Renderの正規所有権を変更する場合は、個別の設計承認と回帰検証を必要とする。
- 各PhaseでDebug / Releaseのビルドと関連テストを行い、未実施項目を明記する。
