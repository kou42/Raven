# RHI Frame Lifecycle 設計・移行計画

## 現状（master の実装に基づく）

- `RHIDevice` は Buffer / Texture 生成を担当し、Frame開始・Presentは持たない。
- `RHICommandList` はScene向け描画命令の抽象化。現在 `RenderCommand::Init()` が構築するのはOpenGL実装のみ。
- `RHIClearContext` はOpenGL / Vulkan / DX12の独立したClear Demoに共通する `Init / DrawClearFrame / Resize / Shutdown` を提供する。これはScene用の一般的なFrame APIではない。
- `ClearBackendDemo` がイベント処理、ゼロサイズ時の描画停止、Resize再試行、成功フレーム数を管理する。Window更新のSwapBuffersとDemo内のPresentを二重に呼ばない。

## 現在のBackend別処理

| 段階 | OpenGL | Vulkan | DX12 |
| --- | --- | --- | --- |
| フレーム準備 | ContextをCurrentにする | Frame Fence待機、SwapChain Image Acquire、CommandBuffer Reset/Begin | Frame Fence待機、CommandList Reset、BackBuffer取得 |
| 描画 | glClear | Image Layout遷移、vkCmdClearColorImage | PRESENT→RENDER_TARGET遷移、RTV Clear |
| 終了・Submit | GL命令はContextへ即時発行 | LayoutをPRESENTへ遷移、End、Fence Reset、Queue Submit | PRESENTへ遷移、Close、ExecuteCommandLists |
| Present | glfwSwapBuffers | vkQueuePresentKHR、Submit済みSlotを進める | SwapChain Present、Execute済みSlotにFence Signal |
| Resize | glViewport | SwapChain Recreate後、Image数に合わせFrameSync再初期化 | Queue Fence待機、ResizeBuffers、RTV再構築 |

## 共通インターフェースの責務（次のコード変更の契約）

上位のFrame orchestrationは `BeginFrame → 描画命令 → EndFrame → Present` の順を保証する。
`BeginFrame` は描画可能な対象の取得とBackend固有同期を行い、`ResizeRequired` を返した場合は描画命令・EndFrame・Presentを呼ばない。
`EndFrame` はコマンド記録終了とSubmitを担当し、成功したSubmitの所有権・Fence管理をBackendに残す。
`Present` は表示と結果判定を担当する。PresentがResizeを要求しても、すでにSubmitしたFrame Slotの進行と同期情報を巻き戻さない。
`Resize` はフレーム外で実行し、GPUが旧BackBufferを参照しないことをBackendが保証する。
`RHIDevice` のリソース生成と `RHICommandList` の描画命令をFrame orchestrationへ移さない。

### 重要な移行上の制約

現状のVulkan/DX12の `FrameRenderer::DrawClearFrame` はAcquireからPresentまで一体である。
これを外側の `BeginFrame/EndFrame/Present` で形式的に包むだけではフレーム分離にならない。
まず各BackendのFrameRendererを段階別に分離し、失敗時のFence・Semaphore・CommandBuffer状態を明示してから共通APIへ切り替える。
`RHIClearContext::DrawClearFrame` は移行期間中の互換経路として残し、各段階を内部で呼ぶ形へ変更する。
OpenGLのWindow所有Context、VulkanのImage別Present Semaphore、DX12のFrame別FenceValueはBackend内部に保持する。

## 実装進捗

- [x] `RHIFrameLifecycle` に BeginFrame / ClearFrame / EndFrame / Present の最小契約を追加。
- [x] OpenGLClearContextが各段階を実装し、従来のDrawClearFrameを互換入口として維持。
- [x] VulkanのAcquire / Clear / Submit / Presentを段階別に分離し、既存のDrawClearFrameを互換入口として維持。
- [x] DX12のFence / Clear / Execute / Presentを段階別に分離し、既存のDrawClearFrameを互換入口として維持。
- [x] ClearBackendDemoをBeginFrame / ClearFrame / EndFrame / Presentの共通呼び出しへ切り替え。
- [x] 共通RunRHIClearFrameへClear Demoと3 Backendの互換入口を集約。
- [x] SceneのWindow側Frame終端をPollEvents / Presentへ分離（OnUpdate互換入口は維持）。
- [x] OpenGL SceneにRHISceneFrameLifecycleのBeginFrame / EndFrame / Presentを接続。
- [x] Scene Frame Lifecycleの生成をBackend選択付きFactoryへ移し、ApplicationのOpenGL具象型依存を解消（Vulkan/DX12は未実装としてnullptr）。
- [x] DX12 FrameRendererでBackBufferのPRESENT→RENDER_TARGET→PRESENTをScene描画区間に分離し、Clear DemoのClearFrame互換入口を維持。
- [ ] Vulkan/DX12のScene用CommandList/RenderPassとRHISceneFrameLifecycle実装。

## Scene Rendererとの接続で確認した現状

- `Application::Run()` は `Renderer::BeginFrame()` でCPU Profilerと描画統計を開始し、Scene / Layer / ImGui / Raven UI描画の後に `Window::OnUpdate()` を呼ぶ。
- `WindowsWindow::OnUpdate()` は互換入口として `PollEvents()` と `Present()` を呼ぶ。Sceneの `Application::Run()` は両者を明示的に呼び、OpenGLのSwapBuffersを一度だけ実行する。
- `RenderCommand::Init()` が作るScene用 `RHIDevice / RHICommandList` はOpenGLのみ。Vulkan/DX12は現状Clear Demoの経路であり、Scene描画を実行できると見なさない。
- OpenGL SceneではFactoryが生成したOpenGLSceneFrameLifecycleがWindowを借用し、Scene / Layer / UIの描画前後とPresentを管理する。Clear DemoのContextは転用しない。
- RHISceneFrameLifecycleはClearを必須としない。SceneのClear命令は既存のRHICommandListが担当し、Clear DemoのRHIFrameLifecycleとは分離する。

## 検証条件

1. OpenGL / Vulkan / DX12のClear Demoで連続120フレームと正常終了。
2. 最小化（Framebuffer 0×0）中はAcquire/Presentしない。復帰後はResizeして描画再開。
3. Vulkan Acquire/PresentのOUT_OF_DATE / SUBOPTIMAL時に適切にResizeし、Submit済みSlotを誤再利用しない。
4. DX12 Present失敗時にもExecute済みCommandListへFenceをSignalし、再利用前にWaitする。
5. SceneのOpenGL描画・Physics Debug経路を維持し、Window更新との二重Swapを起こさない。

> 共通契約・3 BackendのClear経路・共通Frame進行・Scene Windowのイベント/Present分離は実装済みです。OpenGL SceneのFrame境界は接続済みです。Vulkan/DX12 Scene実装・今回の変更のビルド/実行検証は未完了です。

## 次のScene実装単位

1. Vulkan / DX12のScene用Frame ContextをClear Demoから独立して用意し、Windowを借用したAcquire / Submit / PresentとResizeを実装する。
2. SceneのRenderTarget / RenderPass境界を定義し、ClearだけでなくDraw中も適切なImage Layout / Resource Stateを維持する。
3. RHICommandListに各Backendの実装を接続する。現在のPipeline / Texture / VertexArrayはOpenGL具象リソース生成に依存するため、Scene描画可能と宣言する前にBackend別生成を整備する。
4. Scene / Layer / UIの各描画経路を検証し、OpenGLの既存描画とPresent回数を回帰確認する。

この段階のFactory追加はVulkan/DX12のScene描画を有効化しません。`RenderCommand::Init()`も引き続きOpenGLのみであり、Backendを切り替えるだけでは通常Sceneを起動できません。

### DX12 RenderTarget区間（今回追加）

`DX12FrameRenderer::BeginFrame` → `BeginRenderTarget` → Scene Draw命令 → `EndRenderTarget` → `EndFrame` → `Present` の順で呼びます。`EndFrame` はRenderTarget終了前にSubmitしません。従来のClear Demoは `ClearFrame` がBeginRenderTarget / ClearRenderTargetView / EndRenderTargetを内部で呼ぶため、外側の呼び出し順は維持されます。これはDX12 Scene ContextやRHICommandListを接続する前段階であり、まだ通常Sceneを起動しません。Depth Target、複数Pass、Offscreen描画は後続実装です。
