# RHI Migration Checklist

Raven の RHI 移行で残っている Legacy RendererAPI 依存と、移行完了条件を管理するためのチェックリストです。

## 1. 残存 Legacy API 整理

- [x] Scene の Material bind は `Material::Bind()` / `BindForSurface()` から `RenderCommand` → `RHICommandList` へ送る。
- [x] Editor Entity Picking は `RendererAPI` を受け取らない `Material::Bind()` を利用する。
- [x] Editor Selection Outline は `RendererAPI` を受け取らない `Material::Bind()` を利用する。
- [ ] `PhysicsDebugRenderer` の `Material::Bind(Renderer::GetAPI())` を `Material::Bind()` へ移行する。
- [ ] `PhysicsDebugRenderer` の `Renderer::GetAPI().DrawIndexed()` を `RenderCommand::DrawIndexed()` へ移行する。
- [ ] Physics Debug Overlay の `glGetIntegerv(GL_VIEWPORT, ...)` を RHI / Renderer 管理の viewport 情報へ移行する。
- [ ] Physics Debug 移行後、`Material::Bind(RendererAPI&)` 互換 overload を削除する。
- [ ] `Renderer::GetAPI()` / `RenderCommand::GetAPI()` の呼び出し元を再検索し、不要になった公開 Legacy API を削除する。
- [ ] `Renderer::Init()` の OpenGL 固有 `SetAPI(std::make_unique<OpenGLRendererAPI>())` を Backend 選択責務へ寄せる。

### Legacy として数えないもの

`Platform/OpenGL/RHI` 以下の `gl*` 呼び出しは OpenGL Backend 実装そのものなので、上位層の Legacy API 依存とは区別します。`glad.c` / `glad.h` も OpenGL loader のため移行対象外です。

## 2. Physics Debug 経路

目標経路は次の通りです。

`PhysicsDebugRenderer` → `Material` / `RenderCommand` → `RHICommandList` → Graphics Backend

Physics Debug の Line topology は `PipelineSpecification::Topology = PrimitiveTopology::Lines` を保持し、Draw 側で OpenGL 固有 primitive を直接指定しません。

Overlay の画面サイズ取得も OpenGL state query に依存させず、Renderer が設定した viewport を RHI 共通情報として参照できる形へ移します。これにより DirectX / Vulkan Backend 追加時に Physics Debug 側へ API 分岐を追加しない設計を維持します。

## 3. ビルド・動作確認項目

### Compile / Link

- [ ] Debug x64 が compile / link できる。
- [ ] Release x64 が compile / link できる。
- [ ] `RendererAPI&` を要求する不要な Material bind 呼び出しが残っていない。
- [ ] Renderer / Physics / Editor 上位層から OpenGL の `gl*` を直接呼んでいない。

### Scene rendering

- [ ] Opaque Mesh が従来どおり描画される。
- [ ] Transparent Mesh の depth write / blend と描画順が維持される。
- [ ] Entity Picking が正しい Entity ID を返す。
- [ ] Selection Outline が選択 Mesh に表示される。

### Physics Debug

- [ ] `B`: AABB が表示される。
- [ ] `O`: OBB が表示される。
- [ ] `F`: Fat AABB が表示される。
- [ ] `T`: Dynamic AABB Tree が表示される。
- [ ] `P`: Broad Phase Pair が表示される。
- [ ] `C`: Contact Point が表示される。
- [ ] `N`: Contact Normal が表示される。
- [ ] `H`: Solver Statistics Overlay が表示・非表示できる。
- [ ] Debug line が Triangle として統計計上されないことを確認する。

## 次の実装単位

1. Physics Debug の draw / material bind を RHI 標準経路へ置換する。
2. viewport state query を Backend 非依存化する。
3. Physics Debug が Legacy RendererAPI を参照しなくなったことを検索で確認する。
4. Material / Renderer / RenderCommand に残った互換 API を削除する。
5. 上記ビルド・動作確認を実施する。
