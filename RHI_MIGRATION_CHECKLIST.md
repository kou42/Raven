# RHI Migration Checklist

Raven の RHI 移行で残っている Legacy RendererAPI 依存と、移行完了条件を管理するためのチェックリストです。

## 1. 残存 Legacy API 整理

- [x] Scene の Material bind は `Material::Bind()` / `BindForSurface()` から `RenderCommand` → `RHICommandList` へ送る。
- [x] Editor Entity Picking は `RendererAPI` を受け取らない `Material::Bind()` を利用する。
- [x] Editor Selection Outline は `RendererAPI` を受け取らない `Material::Bind()` を利用する。
- [x] `PhysicsDebugRenderer` の Legacy Material bind を `Material::Bind()` へ移行する。
- [x] `PhysicsDebugRenderer` の Legacy DrawIndexed を `RenderCommand::DrawIndexed()` へ移行する。
- [x] Physics Debug Overlay の `glGetIntegerv(GL_VIEWPORT, ...)` を RHI 管理の viewport 情報へ移行する。
- [x] Physics Debug 移行後、Legacy Material bind互換 overload を削除する。
- [x] `Renderer::GetAPI()` / `RenderCommand::GetAPI()` / `SetAPI()` を削除する。
- [x] `Renderer::Init()` の OpenGL 固有 Legacy API生成を削除し、`RHICommandList::Init()` へBackend初期stateを移す。
- [x] `Renderer::Submit()` の直接Shader bind経路をPipeline / RenderCommand経路へ移行して削除する。
- [x] Buffer / VertexArray / Texture / Shader / Pipeline / Framebuffer / UI factoryのBackend判定を `RHIBackend` へ統一する。
- [x] `RendererAPI` / `OpenGLRendererAPI` のLegacy classを削除する。
- [x] OpenGL Pipelineのnative state適用実装を `Renderer/Pipeline` から `Platform/OpenGL` へ移す。
- [x] OpenGL VertexArray / VertexBuffer / IndexBuffer実装を `Renderer/Buffer` から `Platform/OpenGL` へ移す。
- [x] OpenGL Shader実装を `Renderer/Shader` から `Platform/OpenGL` へ移す。
- [x] OpenGL Texture互換Bridgeを `Renderer/Texture` から `Platform/OpenGL` へ移す。

### Legacy として数えないもの

`Platform/OpenGL` 以下の `gl*` 呼び出しは OpenGL Backend 実装そのものなので、上位層の Legacy API 依存とは区別します。`glad.c` / `glad.h` も OpenGL loader のため移行対象外です。

現在のBackend選択は `RHITypes.h` の `RHIBackend` / `GetRHIBackend()` に集約しています。DirectX / Vulkanは識別子のみ定義済みで、実装がないfactoryではOpenGLへ暗黙fallbackせず `nullptr` またはassertで未実装を明示します。

### OpenGL依存の最終分類

- `Platform/OpenGL` 以下のPipeline / Buffer / VertexArray / Shader / Texture / RHI実装はBackend固有コードとして意図的に残します。
- `UI/Rendering/OpenGLUIRenderer.cpp` は `UIRenderer::Create()` から選択されるOpenGL UI Backendです。現状はDrawListのtessellationとOpenGL state保存・復元が同居しているため、RHI移行の完了条件として機械的にPlatformへ移動しません。
- `OpenGLUIRenderer` の `glBindFramebuffer` / viewport / scissor / blend / draw / state restoreをRHI化する場合は、RenderTarget・Scissor・UI用Draw offset等のCommandList APIを先に設計し、UI/SVGの既存描画を維持したまま別実装単位で移行します。
- `Core/Application.cpp` のGLFW利用はWindow/Input/Frame timing境界として現行Platform構成に依存しています。`glad` includeは直接OpenGL描画依存とは分けて扱い、Platform入力抽象化を行う際に整理します。
- Dear ImGuiのOpenGL Backendは外部UI Backendとの統合境界であり、Raven RendererのLegacy RendererAPI依存とは別管理とします。

## 2. Physics Debug 経路

現在の経路は次の形へ移行済みです。

`PhysicsDebugRenderer` → `Material` / `RenderCommand` → `RHICommandList` → Graphics Backend

Physics Debug の Line topology は `PipelineSpecification::Topology = PrimitiveTopology::Lines` を保持し、Draw 側で OpenGL 固有 primitive を直接指定しません。

Overlay の画面サイズ取得は `RenderCommand::GetViewport()` → `RHICommandList::GetViewport()` を使用します。OpenGLではBackend内部だけが `GL_VIEWPORT` を参照するため、Physics Debug側へGraphics API依存を持ち込みません。

Draw統計は `RenderCommand` が現在のPipeline topologyを追跡し、`PrimitiveTopology::Triangles` の場合だけ `TriangleCount` を加算します。Line / Point drawも `DrawCalls` / `IndexCount` には含めますが、Triangleとしては数えません。

## 3. ビルド・動作確認項目

### Compile / Link

- [ ] Debug x64 が compile / link できる。
- [ ] Release x64 が compile / link できる。
- [x] Legacy `RendererAPI&` を要求する Material bind APIを削除済み。
- [x] Physics Debug 上位層からOpenGLの `gl*` 呼び出しを削除済み。
- [x] Legacy `RendererAPI` / `OpenGLRendererAPI` classを削除済み。
- [x] 削除したLegacy RendererAPIファイルはwildcard project構成のため、Visual Studio project metadataに個別参照を残さないことを確認済み。
- [x] Renderer resource / pipeline具体実装のOpenGL依存を `Platform/OpenGL` へ配置し、残るUI / Window / ImGui依存をBackend境界として分類済み。

### Scene rendering

- [ ] Opaque Mesh が従来どおり描画される。
- [ ] Transparent Mesh の depth write / blend と描画順が維持される。
- [ ] Entity Picking が正しい Entity ID を返す。
- [ ] Selection Outline が選択 Mesh に表示される。
- [ ] SandboxのTriangle描画がPipeline経路で従来どおり表示される。

### Physics Debug

- [ ] `B`: AABB が表示される。
- [ ] `O`: OBB が表示される。
- [ ] `F`: Fat AABB が表示される。
- [ ] `T`: Dynamic AABB Tree が表示される。
- [ ] `P`: Broad Phase Pair が表示される。
- [ ] `C`: Contact Point が表示される。
- [ ] `N`: Contact Normal が表示される。
- [ ] `H`: Solver Statistics Overlay が表示・非表示できる。
- [ ] Overlayが現在のFramebuffer / Viewportサイズに追従する。
- [ ] Debug line が Triangle として統計計上されないことを確認する。

## UI RHI化の段階実装

- [x] `RHICommandList::DrawIndexed` / `RenderCommand::DrawIndexed` にIndexBuffer要素単位の `firstIndex` を追加し、OpenGL Backendでbyte offsetへ変換する（コードレビュー済み・ビルド未検証）。
- [x] ScissorのFramebuffer左下原点Pixel矩形と有効/無効をRHICommandList / RenderCommandに追加する（コードレビュー済み・ビルド未検証）。
- [x] `OpenGLUIRenderer` のviewport設定・取得・復元とCommandごとのClip ScissorをRenderCommand経由へ移す（コードレビュー済み・ビルド未検証）。
- [x] Window Overlay用default RenderTargetのbindingをRenderCommand / RHICommandListへ移す（コードレビュー済み・ビルド未検証）。
- [x] Scissorの現在値（無効時の矩形を含む）をRHI経由で取得し、UI終了時に復元する（コードレビュー済み・ビルド未検証）。
- [x] UI OverlayのDraw/Read FramebufferとBuffer選択をRHIで保存・復元する（OpenGL互換state、コードレビュー済み・ビルド未検証）。
- [x] UIが変更するShader / VAO bindingとBlend係数・演算を復元し、後続描画へのstate漏れを防ぐ（コードレビュー済み・ビルド未検証）。
- [ ] Offscreen RenderTargetの共通bindingと残りの描画stateの保存・復元契約を設計する。
- [ ] UI Shader / PipelineのRHI経路を構築し、CommandごとのDrawを `RenderCommand` へ移行する。
- [ ] UI / SVG / Imageの描画と既存3D state復元を実機確認する。

## 次の実装単位

1. masterとの差分を最終レビューし、Legacy RendererAPI削除とOpenGL具体実装移動に取りこぼしがないことを確認する。
2. Debug / Release x64のcompile / linkを確認する。
3. Scene / Sandbox / Physics Debugの実動作を確認する。
4. UI描画の完全RHI化は、RenderTarget / Scissor / Draw offset API設計を含む独立した移行単位として進める。
