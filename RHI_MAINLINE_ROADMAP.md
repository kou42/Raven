# Raven RHI Mainline Integration Roadmap

## 目的

Raven の OpenGL / DirectX 12 / Vulkan を、独立 Demo ではなく通常の `Application` / `Scene` / `Renderer` から利用できる RHI 本流へ統合するためのロードマップです。

この文書は 2026-09-27 時点の作業区切りを記録し、次回の実装再開時に「何が完了していて、何を次に直すべきか」を判断できる正本として使用します。

基盤統合済み Pull Request:

`#288 RHI本流合流に向けてBackend生成境界を整理`（masterへマージ済み）

Phase 1 継続作業ブランチ:

`refactor/rhi-legacy-boundary-audit`

---

## 1. 現在の設計方針

### Explicit Backend に Legacy RHICommandList を追加しない

DirectX 12 / Vulkan は、既存の Explicit Scene RHI を本流として使用します。

```text
Application
    |
    +-- OpenGL
    |     |
    |     +-- RenderCommand
    |     +-- RHICommandList
    |     +-- OpenGL Scene Frame Lifecycle
    |
    +-- DirectX 12 / Vulkan
          |
          +-- IExplicitSceneRuntime
          +-- RHIDevice
          +-- RHISceneCommandList
          +-- RHISceneFrameLifecycle
```

DX12 / Vulkan 用の Legacy `RHICommandList` を新設して `RenderCommand` に合わせる方法は採用しません。

Explicit Runtime が Native Context / Device / Pipeline / Frame Lifecycle を所有し、Application は Runtime が公開する共通境界だけを使用します。

### Frame の基本順序

Explicit Backend の通常 Scene は次の順序を基本契約とします。

```text
Scene / Layer Update
        |
Renderer Scene Queue 構築
        |
Debug / UI CPU Queue 構築
        |
IExplicitSceneRuntime::PrepareFrame()
        |
RHISceneFrameLifecycle::BeginFrame()
        |
Scene Draw
Debug Draw
UI Draw
        |
EndFrame / Submit
        |
Present
```

1 Frame 内で Scene / Debug / UI を記録し、Submit / Present は一度だけ行います。

---

## 2. 完了済み

### Backend / Runtime 境界

- [x] Legacy Backend 生成を `RHILegacyBackendFactory` へ分離。
- [x] Explicit Runtime 生成を `RHIExplicitSceneRuntimeFactory` へ分離。
- [x] `RHIExplicitSceneSpecification` を追加。
- [x] `ApplicationSpecification::ExplicitScene` を追加。
- [x] OpenGL と DX12/Vulkan で Frame Lifecycle の所有関係を分離。
- [x] DX12/Vulkan に Legacy `RHICommandList` を追加しない方針を確定。

### 通常 Application と Explicit Runtime

- [x] `Application` が DX12/Vulkan の `IExplicitSceneRuntime` を所有。
- [x] Scene 切替時に Explicit Runtime の `PrepareScene()` を再実行。
- [x] Explicit Scene Queue モードを Renderer に追加。
- [x] 通常 Frame を `PrepareFrame -> BeginFrame -> DrawPreparedFrame` 系へ接続。
- [x] SceneGame の Clear / Viewport を Explicit Backend では Runtime 側へ委譲。
- [x] `--backend=vulkan` / `--backend=dx12` を独立 Demo ではなく通常 Application 起動へ接続。
- [x] `--scene-vulkan` / `--scene-dx12` は独立 Runtime Demo として維持。

### Physics / Animation Debug

- [x] CPU Debug Line Queue を Renderer に追加。
- [x] Physics Debug / Animation Debug を Explicit RHI Buffer へ変換。
- [x] DX12/Vulkan に Lines topology 用 Debug Pipeline を追加。
- [x] Surface + Debug Line を同じ Explicit Frame に記録。
- [x] Debug 描画後も End/Present を一度だけ実行。
- [x] Viewport 取得を Backend 非依存化。
- [x] `RHIViewport` を共通 RHI 型へ移動。

### Raven UI

- [x] OpenGL UI の CPU tessellation と GPU submission の責務を調査。
- [x] Explicit UI 用の CPU Draw Data 境界を追加。
- [x] UI draw command / vertex / index / clip / texture 情報を Explicit Frame へ渡す経路を追加。
- [x] DX12/Vulkan の UI Pipeline / Buffer / Command recording を追加。
- [x] Scene / Debug / Raven UI を同一 Explicit Frame へ統合。
- [x] Main Window の Raven UI を Explicit Backend へ接続。
- [x] OpenGL UI の既存描画経路を維持。

### Scene Resource の Legacy 依存整理

- [x] SceneGame の床 / 影 Mesh を CPU `MeshGeometry` 正規経路へ移行。
- [x] Explicit Backend では `Mesh` の Legacy VAO/VBO 自動生成を抑制。
- [x] Explicit Backend の Material 用に PipelineSpecification holder を使用。
- [x] SceneGame の Legacy Shader / Texture 生成を OpenGL 経路へ限定。
- [x] SoftBody Cloth / Jelly / Fluid SPH Demo Material を Explicit Scene でも構築可能化。

---

## 3. 意図的に OpenGL 専用として残す境界

以下は「RHI移行漏れ」として機械的に削除しません。

### Dear ImGui

現状の `ImGuiLayer` は以下へ直接依存しています。

- `imgui_impl_glfw`
- `imgui_impl_opengl3`

DX12/Vulkan 通常 Application では Dear ImGui を無効化します。

Dear ImGui の DX12/Vulkan Backend 対応は Raven UI 本体の RHI 化とは別タスクとして扱います。

### Auxiliary / Multi Window UI

補助 Window は OpenGL Context / VAO / default framebuffer を利用しています。

初期 Explicit RHI 対応の対象は Main Window の Raven UI のみです。

DX12/Vulkan の補助 Window 対応には SwapChain / Frame Lifecycle / Resource ownership の設計が必要なため、別フェーズに分離します。

### Platform/OpenGL

`Platform/OpenGL` 以下の `gl*` 呼び出しは OpenGL Backend 実装そのものなので Legacy 移行対象には含めません。

---

## 4. 現在の既知リスク

### 4.1 実ビルド未確認

現在の RHI 本流統合変更は GitHub 上での静的レビューが中心です。

最新 HEAD に対して以下は未実施です。

- Debug x64 compile / link
- Release x64 compile / link
- OpenGL 通常 Application 実行
- DX12 通常 Application 実行
- Vulkan 通常 Application 実行

ビルドエラーが出た場合は、機能追加より先に本流の compile/link を回復します。

### 4.2 DX12 Debug Pipeline の Vertex Input

Scene shader の VS input と Debug Line vertex layout の整合性を実ビルド / Debug Layer で確認します。

特に SceneMesh HLSL 側の NORMAL input と Debug Pipeline 側の Position / Color / TexCoord 構成が PSO 作成条件と一致するか確認が必要です。

必要なら Debug 専用 shader を追加し、Scene shader の入力契約から分離します。

### 4.3 Debug Line の Depth semantics

現在の Debug Line は Overlay 寄りの Depth 無効設定を使用しています。

将来的には用途を分離します。

- World Debug Line: Depth Test 有効
- Screen / Statistics Overlay: Depth Test 無効

### 4.4 Debug / UI Buffer の毎 Frame 生成

現段階では correctness と RHI 境界確立を優先しています。

Buffer 再利用、ring buffer、frame allocator 等の最適化は動作確認後に行います。

### 4.5 Dynamic Mesh

SoftBody / Cloth / Wave 等の Dynamic Geometry が Explicit RHI Buffer へ正しく revision 同期されることを実機確認します。

特に Scene 切替、Mesh 再生成、Resize と同時に発生した場合の Resource lifetime を確認します。

---

## 5. 次回再開時の実装順

### Phase 1: 残存 Legacy 到達経路の最終監査

- [x] 通常 Application から到達可能な `RenderCommand` 直接利用を再検索。
- [x] `Scene` / `Layer` / `Renderer` 上位層の `gl*` 直呼びを再検索。
- [x] OpenGL 専用コードは明示的な Backend guard 内にあることを確認。
- [x] Dear ImGui / Auxiliary Window を「意図的 OpenGL 境界」としてコメント・文書上で確定。
- [x] `RHILegacyBackendFactory` 実装配置を必要なら専用 cpp へ整理。

完了条件:

通常 DX12/Vulkan Application の Scene Frame が Legacy OpenGL Resource / Context を要求しないこと。

### Phase 2: 静的整合性レビュー

- [x] DX12 Graphics Pipeline の input layout と Debug/UI shader を確認。
- [x] Vulkan vertex attribute / descriptor / dynamic viewport/scissor を確認。
- [x] Debug/UI texture binding の Resource lifetime を確認。
- [x] `offsetof` 等の必要 standard header を確認。
- [x] Scene 切替後の `PrepareScene()` と GPU Resource 再構築を確認。
- [x] Fatal / ResizeRequired 時の Prepared Frame 破棄経路を確認。

完了条件:

GitHub 上のコードレビューで明らかな compile error / ownership error / frame lifecycle violation が残っていないこと。

### Phase 3: Compile / Link

ローカルビルド環境が必要です。

- [ ] Debug x64 build。
- [ ] Release x64 build。
- [ ] OpenGL / DX12 / Vulkan の Backend 固有 source が正しく project に含まれる。
- [ ] shader asset / DXIL / SPIR-V path が実行時 working directory と一致。
- [ ] warning / linker error を記録して修正。

完了条件:

Debug / Release x64 が compile / link 成功。

### Phase 4: Backend Smoke Test

#### OpenGL

- [ ] 通常 Application 起動。
- [ ] Scene 描画。
- [ ] Physics / Animation Debug。
- [ ] Raven UI。
- [ ] Dear ImGui。
- [ ] Resize / minimize / restore。
- [ ] Scene 切替。
- [ ] 正常 Shutdown。

#### DirectX 12

- [ ] `--backend=dx12` で通常 Application 起動。
- [ ] Scene Mesh 描画。
- [ ] Dynamic Mesh 更新。
- [ ] Physics / Animation Debug Line。
- [ ] Raven UI。
- [ ] Resize / minimize / restore。
- [ ] Scene 切替。
- [ ] Debug Layer / Live Object Report。
- [ ] 正常 Shutdown。

#### Vulkan

- [ ] `--backend=vulkan` で通常 Application 起動。
- [ ] Scene Mesh 描画。
- [ ] Dynamic Mesh 更新。
- [ ] Physics / Animation Debug Line。
- [ ] Raven UI。
- [ ] Resize / minimize / restore。
- [ ] Scene 切替。
- [ ] Khronos Validation Layer。
- [ ] 正常 Shutdown。

完了条件:

3 Backend の通常 Application が同じ Scene / Renderer 契約で起動し、重大な Validation error なしで終了できること。

### Phase 5: Correctness 修正

Smoke Test で発見した問題だけを優先して修正します。

想定項目:

- [ ] DX12 Debug vertex input mismatch。
- [ ] Vulkan descriptor / image layout。
- [ ] UI scissor / viewport 原点差。
- [ ] Texture UV 上下差。
- [ ] Transparent pass / blend。
- [ ] Debug depth。
- [ ] Resize 後の stale resource。
- [ ] Dynamic Mesh revision synchronization。

新機能追加はこのフェーズでは原則行いません。

### Phase 6: 最適化

Correctness 確認後に着手します。

- [ ] Debug Line dynamic buffer 再利用。
- [ ] UI vertex/index buffer 再利用。
- [ ] Descriptor / texture binding cache。
- [ ] Pipeline cache。
- [ ] Frame allocator / ring buffer 検討。
- [ ] unnecessary CPU copy の計測。
- [ ] draw call / upload counter の追加。

---

## 6. RHI 本流合流の完了条件

以下をすべて満たした時点で、今回の RHI Mainline Integration を完了とします。

- [ ] OpenGL / DX12 / Vulkan が通常 `Application` から選択可能。
- [ ] 3 Backend で通常 Scene が描画可能。
- [ ] Physics / Animation Debug が 3 Backend で動作。
- [ ] Main Window Raven UI が 3 Backend で動作。
- [ ] Scene 切替が 3 Backend で動作。
- [ ] Resize / minimize / restore が 3 Backend で動作。
- [ ] Dynamic Mesh が DX12/Vulkan でも更新される。
- [ ] Explicit Backend が Legacy OpenGL Context / Resource を要求しない。
- [ ] Frame ごとの Submit / Present が一度だけ行われる。
- [ ] Debug Layer / Validation Layer に重大なエラーがない。
- [ ] Debug x64 / Release x64 が build 成功。
- [ ] 意図的な OpenGL 専用境界が文書化されている。

Dear ImGui の DX12/Vulkan Backend と Explicit Multi Window は、この完了条件には含めません。

---

## 7. RHI 本流合流後の候補

優先度は本流安定化後に決定します。

### Dear ImGui Backend

- ImGui DX12 renderer backend
- ImGui Vulkan renderer backend
- GLFW platform backendとの責務整理
- Descriptor pool / texture ID abstraction

### Explicit Multi Window

- Window ごとの SwapChain
- Window ごとの Frame Lifecycle
- Device 共有
- Resource ownership
- Main / Auxiliary Window の同期

### Rendering Feature

- Offscreen Render Target
- Multi Pass
- Shadow Map
- MSAA
- Post Process
- Render Graph
- Compute
- Async upload

---

## 8. 再開時チェックポイント

次回作業を開始するときは、最初に以下を確認します。

1. `master` と現在のRHI作業ブランチの差分。
2. 直近RHI Pull Requestの HEAD / merge conflict / CI 状態。
3. この文書の Phase 1 未完了項目。
4. `RHI_MIGRATION_CHECKLIST.md` と本ロードマップの内容に矛盾がないか。
5. `docs/RHI-frame-lifecycle.md` の古い「DX12/Vulkan Scene未接続」記述が現実装と食い違っていないか。
6. 最新 HEAD の Debug / Release build が未実施なら、機能追加前に build を優先する。

### 再開時の推奨開始点

```text
残存Legacy到達経路の最終監査
        ↓
静的整合性レビュー
        ↓
Debug x64 build
        ↓
DX12 / Vulkan / OpenGL smoke test
        ↓
Correctness修正
        ↓
Release x64 build
        ↓
作業PR 最終レビュー
        ↓
master merge
```

---

## 関連文書

- `RHI_MIGRATION_CHECKLIST.md`
- `docs/RHI-frame-lifecycle.md`
- `docs/RHI-validation-smoke-test.md`
- `RAVEN_UI_ROADMAP.md`

本ロードマップでは「RHIを通常 Raven Application の本流へ合流すること」に焦点を限定し、各サブシステムの詳細設計は関連文書へ分離します。
