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
- [ ] VulkanのAcquire / Submit / Presentを段階別に分離。
- [ ] DX12のFence / Execute / Presentを段階別に分離。
- [ ] ClearBackendDemoの共通Frame呼び出しへの切り替え。
- [ ] Scene Rendererとの統合。

## 検証条件

1. OpenGL / Vulkan / DX12のClear Demoで連続120フレームと正常終了。
2. 最小化（Framebuffer 0×0）中はAcquire/Presentしない。復帰後はResizeして描画再開。
3. Vulkan Acquire/PresentのOUT_OF_DATE / SUBOPTIMAL時に適切にResizeし、Submit済みSlotを誤再利用しない。
4. DX12 Present失敗時にもExecute済みCommandListへFenceをSignalし、再利用前にWaitする。
5. SceneのOpenGL描画・Physics Debug経路を維持し、Window更新との二重Swapを起こさない。

> 共通契約とOpenGL経路のみ実装済みです。Vulkan/DX12の分離・ビルド・実行検証は未完了です。
