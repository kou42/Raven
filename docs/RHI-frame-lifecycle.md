# RHI Frame Lifecycle 設計・移行計画

## 2026-09-27 RHI本流合流後の現在地

PR #288 の統合により、通常 `Application` は OpenGL では Legacy `RenderCommand / RHICommandList`、
DirectX 12 / Vulkan では `IExplicitSceneRuntime / RHISceneCommandList` を使用します。
DX12/Vulkan は Clear Demo 専用ではなく、通常 Scene / Physics・Animation Debug / Main Window Raven UI を
同一 Explicit Frame に記録する経路へ接続済みです。

`RHISceneFrameLifecycle::Create()` と Legacy `RenderCommand` が OpenGL のみを生成する点は意図的です。
Explicit Backend を Legacy `RHICommandList` へ追加せず、Runtime が Native Context / Device / Pipeline /
Frame Lifecycle を所有します。Dear ImGui と Auxiliary Window は引き続き意図的な OpenGL 専用境界です。

> 以下には移行過程の履歴説明も残しています。「Vulkan/DX12 Scene未接続」「通常Application未接続」などの
> 記述は当時の実装段階を説明する履歴であり、現在の本流状態はこの節と
> `RHI_MAINLINE_ROADMAP.md` を正とします。

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

### Vulkan Scene Color Target区間（今回追加）

`VulkanFrameRenderer::BeginFrame` → `BeginSceneColorTarget` → RenderPass開始 / Scene Draw / RenderPass終了（後続実装）→ `EndSceneColorTarget` → `EndFrame` → `Present` の順です。Begin/EndSceneColorTargetはSwapChain Imageを `UNDEFINED` または `PRESENT_SRC_KHR` → `COLOR_ATTACHMENT_OPTIMAL` → `PRESENT_SRC_KHR` へ遷移させるのみで、RenderPassやPipelineはまだ生成しません。SubmitのAcquire Semaphore待機StageはScene時 `COLOR_ATTACHMENT_OUTPUT`、従来Clear Demo時 `TRANSFER` です。両描画経路を同一Frame内で混在させることは未対応です。

### Vulkan Scene RenderTarget（今回追加）

`VulkanSceneRenderTarget` はSwapChain ImageごとにImageView/Framebufferを生成し、Color AttachmentのみのRenderPassを所有します。RenderPassのinitial/final Layoutは両方 `COLOR_ATTACHMENT_OPTIMAL` で、前後のLayout遷移は `VulkanFrameRenderer::BeginSceneColorTarget` / `EndSceneColorTarget` が担当します。`BeginFrame` 成功後の `GetAcquiredImageIndex()` で対応するFramebufferを選択します。想定順序は `BeginFrame` → `BeginSceneColorTarget` → `VulkanSceneRenderTarget::Begin` → Draw → `VulkanSceneRenderTarget::End` → `EndSceneColorTarget` → `EndFrame` → `Present` です。

SwapChain Resize時はGPU完了を待ってからScene RenderTargetのFramebuffer/ImageViewを先に `Shutdown` し、SwapChainを再生成した後に `Init` してください。今回追加したのはRenderPass/Framebufferの部品であり、Scene専用Contextへの接続、Depth Attachment、Pipeline、RHICommandListはまだ未実装です。

### Vulkan Scene専用Frame Context（今回追加）

`VulkanSceneContext` は `RHISceneFrameLifecycle` を実装し、Clear Demoとは独立してVulkan Instance/Surface/SwapChain/FrameSync/CommandBuffer/SceneRenderTargetを所有します。`Init(Window&)` 後に `BeginFrame`（Acquire→Color Layout遷移→RenderPass開始）→Scene Draw記録→`EndFrame`（RenderPass終了→Present Layout遷移→Submit）→`Present` を呼びます。`GetActiveCommandBuffer()` と `GetRenderPass()` は後続のScene用CommandList/Pipeline接続点です。Clear色はBeginFrame前に `SetClearColor()` で指定します。

`Resize` はFrame外でのみ呼び、WaitIdle後に古いRenderTargetを先に破棄してからSwapChain/FrameSync/RenderTargetを再生成します。Acquire後のFatalErrorは同期状態が不明になり得るため、同Contextで継続せず `Shutdown` → `Init` が必要です。`RHISceneFrameLifecycle::Create` は引き続きOpenGLのみを返し、RenderCommandのVulkan経路も未接続です。Scene用RHICommandList/Pipelineが揃うまでApplicationに接続しません。

### DX12 Scene専用Frame Context（今回追加）

`DX12SceneContext` は `RHISceneFrameLifecycle` を実装し、Clear Demoとは独立したFactory/Adapter/Device/Queue/SwapChain/Fence/Frame別CommandList/FrameRendererを所有します。`BeginFrame` でFence待機・CommandList Reset・BackBufferをRENDER_TARGETへ遷移・Clearし、`EndFrame` でPRESENTへ遷移してSubmit、`Present` で表示・Fence Signal・Frame Slot進行を行います。Scene用DrawはBeginFrameとEndFrameの間で `GetActiveCommandList()` に記録します。Clear Demoの `ClearFrame` は従来どおりRenderTargetを内部で閉じ、新しい `ClearRenderTarget` はSceneの描画区間を開いたままClearします。

ResizeはFrame外でFence完了後にSwapChainとRTVを再生成します。FatalError後は同Contextを再利用せず `Shutdown` → `Init` してください。DX12/VulkanともScene専用Contextは部品として追加済みですが、Scene用RHICommandList/Pipeline/ResourceとApplicationのResizeRequired処理が揃うまで `RHISceneFrameLifecycle::Create` には登録しません。

### Scene Viewport/Scissor（今回追加）

Vulkan/DX12 Scene Contextに `SetViewport(x,y,width,height)` を追加し、各FrameのRenderPass/RenderTarget開始後にSwapChain全体のViewport/Scissorを初期設定します。Vulkanは `vkCmdSetViewport` / `vkCmdSetScissor` を記録し、将来のGraphics Pipelineでは `VK_DYNAMIC_STATE_VIEWPORT` / `VK_DYNAMIC_STATE_SCISSOR` を有効にする必要があります。DX12は `RSSetViewports` / `RSSetScissorRects` を記録します。VulkanはSwapChain Extent外の矩形を拒否し、DX12はScissor座標の整数オーバーフローを拒否します。

既存の `RHICommandList` はPipeline/Texture/Uniform/Indexed Drawを一体として要求するため、未実装のResource/Pipelineを成功扱いする空実装は追加していません。今回のViewportはNative Scene Context内の準備段階であり、`RenderCommand` のBackend切替はまだ行いません。

### DX12 Scene Buffer（今回追加）

`DX12SceneBuffer` は既存のOpenGL用 `VertexBuffer` / `IndexBuffer` を無理に置換せず、Scene用のNative GPU Resourceとして追加しました。`InitVertex(device,data,byteSize,stride)` と `InitIndex(device,indices,count)` でUpload Heap Bufferを作り、Vertex/Index Buffer Viewを生成します。Index形式は `DXGI_FORMAT_R32_UINT` です。`SetData` は容量内のCPU書き込みのみを担当し、GPUが読み取り中のBufferを書き換えないよう呼び出し側でFence同期してください。部分更新後もViewのサイズは初期容量のままです。現時点ではDefault Heapへのコピー、Pipeline/Root Signature、Native DrawIndexedの接続は未実装です。

### Vulkan Scene Buffer（今回追加）

`VulkanSceneBuffer` は `InitVertex(device,data,byteSize,stride)` / `InitIndex(device,indices,count)` で `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT` / `VK_BUFFER_USAGE_INDEX_BUFFER_BIT` の `VkBuffer` と `VkDeviceMemory` を所有します。Memory Typeは `HOST_VISIBLE | HOST_COHERENT` を要求し、`SetData` は `vkMapMemory` → コピー → `vkUnmapMemory` で容量内のCPU更新を行います。対応するMemory Typeがない場合は失敗し、Staging Buffer転送へのフォールバックは今後実装します。Index形式は32-bit unsigned integerです。

DX12/Vulkanとも、GPUが読み取り中のBufferの更新・破棄は呼び出し元でFence等による完了確認が必要です。Vulkan Bufferの破棄はVulkanDeviceより前に行ってください。今回のBufferは既存OpenGL `VertexBuffer` / `IndexBuffer` とまだ接続せず、Graphics PipelineとIndexed Drawの実装後に統合します。

### Scene Frame共通Resize契約（今回追加）

`RHISceneFrameLifecycle` に `Resize(width, height)` を追加し、OpenGL / Vulkan / DX12で同じFrame外呼び出し口を用意しました。幅・高さ0は最小化中として呼び出し側で保留し、各実装も失敗を返します。OpenGLではWindowのResize Event経由のViewport更新を維持し、Frame外かつ正のサイズなら成功を返します。Vulkan / DX12は既存の同期・SwapChain再生成処理を `override` で公開します。

**注意:** これはScene描画のBackend切替を有効化する変更ではありません。`RHISceneFrameLifecycle::Create` と `RenderCommand::Init` は従来どおりOpenGLのみ対応します。VulkanのResize後はRenderPass依存Pipelineを再生成する必要があり、Application側の `ResizeRequired` / 最小化処理とScene Resourceの再生成は後続で接続します。DX12についても同じ共通契約を維持します。

### Vulkan Scene Buffer部分更新（追加）

`VulkanSceneBuffer::SetData(data, byteSize, offset = 0)` は既存の全体先頭更新を維持しつつ、`RHIBuffer::SetData` と同様のoffset付き部分更新に対応します。offset/sizeは減算形式で容量を検証し、範囲外・空更新・null dataを拒否します。VkDeviceMemoryは割当先頭からMapしてCPU pointerにoffsetを加算し、Map offsetのalignment制約を回避します。Host Coherentのため明示Flushは不要です。

この変更だけでは `VulkanSceneBuffer` はまだ `RHIBuffer` の実装ではありません。共通Buffer化にはGPU同期とDevice寿命の契約、RHIBuffer派生とRHIDevice::CreateBuffer接続を確定する必要があります。OpenGLは変更していません。

検証項目：先頭・中間・末尾の部分更新、容量境界とoverflow拒否、既存Triangle描画、GPU完了待ち後の更新、OpenGL回帰。ビルド・GPU実機検証は未実施です。

### Vulkan Scene Buffer Resize（追加）

`VulkanSceneBuffer::Resize(byteSize, data = nullptr)` はVertexのstride境界またはIndexのuint32_t境界を検証します。同容量の場合は任意の全体更新のみ実行します。容量変更時は新しいVkBuffer/VkDeviceMemoryを先に確保し、成功した場合だけ旧Resourceと入れ替えます。失敗時は旧Resourceを保持します。IndexCountは新容量から再計算し、VertexStrideは維持します。`data == nullptr` の新規領域は未初期化です。

**重要：** Resize前にGPUの旧Buffer参照が完了している必要があります。成功後はVkBuffer handleが変わるため、古いhandleをキャッシュした描画処理は再取得してください。Scene Context/DeviceのShutdown前にBufferを破棄してください。これは共通RHIBuffer派生・RHIDevice::CreateBuffer接続を完了したことを意味しません。

追加検証項目：Vertex/Index容量変更、同容量更新、stride不整合拒否、確保失敗時の旧handle維持、Resize後のDrawIndexed、GPU同期、Validation Layer。ビルド・実機実行は未確認です。

### Vulkan Scene RHIBuffer接続（追加）

`VulkanSceneRHIDevice::CreateBuffer` は `VulkanSceneRHIBuffer` を通して、Context所有DeviceにVertex/Index用のHost Visible + Coherent Bufferを生成します。初期データ省略も可能です（内容は未定義）。サイズ0・uint32_t上限超過・Indexの4byte非整列・未対応用途（Uniform/Storage等）は `nullptr` を返します。`RHIBuffer::SetData/Resize` のvoid APIではnative失敗を上位へ返せないため、失敗時は既存状態を維持します。

`RHIBufferSpecification` はVertex Strideを持たないため、共通Device生成のVertex Bufferはbyte単位（stride=1）で作成します。**そのままSceneのDrawIndexedに渡しても通常のVertex PipelineのStride検証を通りません。** 後続でPipeline入力宣言との対応を実装してください。既存Triangleは従来のVulkanSceneBuffer経路を維持します。

Contextが所有するVkDeviceより前に、外部が保持するRHIBufferをすべて破棄してください。SetData/Resize/破棄の前にはGPU読み取り完了の同期が必要です。Texture生成と通常SceneのVulkan切替は未対応です。PR #220のPipeline生成Adapterをこのブランチにも含むため、マージ順によっては同一ファイルの重複を整理してください。

追加検証項目：Vertex/Index生成、初期データ省略、Indexサイズ境界、offset更新、Resize後のSpecification更新、未対応用途の拒否、Contextより前のBuffer破棄、OpenGL回帰。ビルド・GPU実機検証は未実施です。

### Vulkan Sceneの共通Buffer描画（追加）

`VulkanSceneCommandList::DrawIndexed(Ref<RHIBuffer> vertex, Ref<RHIBuffer> index, uint32_t vertexStride, uint32_t indexCount = 0)` を追加しました。Vertex/Index用途、VulkanSceneRHIBuffer型、Contextと同一VkDevice、Pipeline Binding 0のStride一致、Vertex容量のStride整列、Index数を検証してから描画します。native VulkanSceneBuffer版のDrawIndexedは引き続き使用できます。

`VulkanSceneTriangleDemo` はContext所有Deviceの `VulkanSceneRHIDevice::CreateBuffer` でVertex/Indexを生成し、共通Buffer版DrawIndexedを通るように変更しました。終了時はWaitIdle後にBufferのRefをresetし、ContextのVkDeviceより先に解放します。Resize後もBufferは維持し、Pipelineのみ再生成します。

Strideは現状CommandListへ明示的に渡す暫定仕様です。一般Sceneへの接続時にはVertex Input Layoutの共有、複数Binding、Buffer lifetimeとGPU同期を共通設計に移す必要があります。ビルド・GPU実機・Validation Layerは未検証です。確認項目：Triangle表示、Resize後再描画、誤ったStride/用途/Device/IndexCountの拒否、終了時Validation Layer、OpenGL回帰。

### 共通Vertex StrideのPipeline入力への一本化

共通RHIBuffer版 `DrawIndexed(vertex, index, indexCount = 0)` は、Bind済PipelineのBinding 0からStrideを取得します。呼び出し側でStrideを重複指定する必要はありません。Bindingが0以外・複数・Stride 0、Vertex容量がStrideで割り切れない場合は描画を拒否します。従来のnative SceneBuffer版はBuffer保持StrideとPipeline Strideの一致を検証します。

本変更は単一Vertex Binding限定です。複数Binding/Instance Rate、頂点範囲やIndex値のCPU検査は未対応です。共通Bufferはbyte単位で確保する仕様のままであり、GPU同期とDevice寿命の制約も変わりません。ビルド・GPU実機・Validation Layerは未検証です。

### Vulkan Scene共通BufferのGPU同期とDevice寿命（追加）

`VulkanSceneRHIBuffer::TrySetData/TryResize` はContextの `SynchronizeBufferAccess` を通し、Frame外かつ未Submit状態で `VkDeviceWaitIdle` に成功した場合だけnative Bufferを変更します。既存の共通 `RHIBuffer::SetData/Resize` はvoid APIのため失敗を通知できません。更新成否が必要なScene固有コードではTry版を使用してください。安全性優先の暫定実装であり、毎回Device全体を待つためDynamic Bufferの頻繁な更新には適しません。

Device経由で生成したBufferはContextがweak参照で追跡し、Context::ShutdownのWaitIdle後、VkDevice破棄前にnative Bufferを無効化します。外部に残ったRefの更新は失敗し、破棄時に破棄済みVkDeviceを呼びません。共通RHIBuffer版DrawIndexedで記録に成功したBufferはContextが強参照を保持し、WaitIdle成功時・Resize・Shutdownで解放します。native VulkanSceneBuffer版の寿命は従来どおり呼び出し側の責務です。別スレッドから同時に操作することは未対応です。Context破棄後のAdapter使用は不可です。

確認項目：Frame外のTrySetData/TryResize成功、BeginFrame～EndFrame中の拒否、Submit後Present前の拒否、Context::Shutdown後に外部Refを破棄してもVulkan呼び出しなし、Triangle表示とResize、OpenGL回帰。ビルド・GPU実機・Validation Layerは未検証です。

### 共通Buffer更新のFrame Fence同期（追加）

`SynchronizeBufferAccess` は従来の `VkDeviceWaitIdle` を廃止し、このSceneがSubmitに成功したFrame SlotのFenceを `vkWaitForFences(..., VK_TRUE, ...)` で待機します。未Submitの初期Signal Fenceは待機対象に含めません。Frame記録中とSubmit後Present前の更新拒否は維持します。GPU完了後にSlot別のBuffer強参照を解放します。

`BeginFrame` では既存FrameRendererが該当SlotのFenceを待機した後、旧Buffer参照を回収します。Submit成功時だけSlotを待機対象に登録します。Resize/ShutdownではSwapChain/Presentation Resourceも扱うため、従来どおりDeviceWaitIdleを維持します。Buffer Destructorは参照回収中の再入同期を避け、Draw成功時のContext強参照によってGPU利用中の破棄を防ぎます。

制約：本方式はSceneのGraphics QueueへSubmitしたBufferのみ追跡します。外部Queue/Scene外での同一Buffer使用、別スレッド操作は未対応です。更新時には使用Buffer個別ではなく全Submit済みScene Frameを待ちます。Frame FenceはPresent EngineによるSemaphore消費完了を保証しないため、SwapChainの再生成・終了はDeviceWaitIdleのままです。ビルド・実機・Validation Layer未検証。

### 共通Buffer単位のFrame Fence待機（追加）

`VulkanSceneRHIBuffer::TrySetData/TryResize` は更新対象自身を `VulkanSceneContext::SynchronizeBufferAccess(buffer)` に渡します。Contextは各Submit済みFrame Slotの記録済み `RHIBuffer` を同一Resourceのポインタで照合し、そのBufferを実際に描画で使用したSlotのFenceだけを `vkWaitForFences` で待機します。未使用Bufferの更新ではFence待機を発行しません。同一Bufferを複数Frameで使用した場合は、該当する全Frameを待ちます。

**重要：** 対象BufferのFence待機が完了しても、同じFrameの別BufferはGPU使用中かもしれません。このため更新時にはFrame Slot全体の強参照やSubmit状態を消去せず、既存の `BeginFrame` のSlot Fence待機後に回収します。これにより他Bufferの早期破棄を避けます。Frame中/Submit後Present前の更新拒否、Resize/ShutdownのDeviceWaitIdleは維持します。

前節の「全Submit済みScene Frameを待つ」は本変更より前の仕様です。対象は共通RHIBuffer版DrawIndexedで追跡したScene Graphics Queue利用のみです。外部Queue、native SceneBuffer直接利用、別スレッドの同時更新は対象外です。確認項目：未使用Bufferの更新、別Bufferだけを使用したFrameの待機省略、同一Bufferを2 Frameで使用した場合の両Fence待機、対象Buffer更新後の他Bufferの寿命、Resize/Shutdown、Validation Layer。ビルド・実機未検証。

### Frame Slot内のBuffer参照重複排除（追加）

`m_RecordedBuffers` をSlotごとの `unordered_map<const RHIBuffer*, Ref<RHIBuffer>>` に変更しました。同一Bufferを同じFrame内で繰り返しDrawしても一度だけ登録し、強参照の重複と更新対象Bufferの線形検索を避けます。Mapのキーは識別用の非所有ポインタですが、値のRefが対応ResourceをGPU完了まで保持します。Frame SlotのFence待機後・Resize・Shutdownの参照回収は従来どおりです。

Buffer別Fence同期は各Submit済みSlotでキーの存在を調べます。計算量は従来の「Slot数 × 各SlotのDraw Buffer登録数」から、平均的に「Slot数 × Mapキー検索」になります。重複Drawが多いSceneほど強参照数も削減します。Frameごとの異なるBuffer数に応じたMapのメモリ使用とハッシュ管理コストは発生します。外部Queue/別スレッドは引き続き対象外です。

確認項目：同一Vertex/Index Bufferを繰り返し描画、異なるBufferの混在、同一Bufferの複数Frame使用、Fence完了後のRef回収、Resize/Shutdown、Validation Layer。ビルド・実機未検証。

### Vulkan Sceneの2 Mesh描画検証経路（追加）

`VulkanSceneTriangleDemo` は左右2つの三角形を、独立したVertex BufferとIndex Buffer（合計4本）で描画する構成に変更しました。同一PipelineをBindした後、同一FrameのCommandListに2回DrawIndexedを記録します。共通RHIDeviceによるBuffer生成、PR #228のFrame Slot別重複排除、PR #227のBuffer別Fence待機を、複数Resourceが混在する実際の描画経路で確認するための変更です。

Resize時は従来どおりPipelineだけを再生成し、2 MeshのBufferを保持します。Shutdown時はGPU完了待機後に4本のBuffer Refを解放し、ContextのDeviceを破棄します。既存の`--scene-triangle-vulkan`起動方法・SPIR-V・通常OpenGL Applicationは変更していません。

確認項目：左右2つの色付き三角形が同時に表示されること、連続Frame描画、最小化・Resize後の再描画、途中Buffer生成失敗時のShutdown、Validation LayerのBuffer寿命エラーなし、OpenGL回帰。ビルド・GPU実機・Validation Layer未検証。

### Vulkan Scene CommandListのColor Clear（追加）

`VulkanSceneCommandList::ClearColor(color)` は `VulkanSceneContext::ClearColorAttachment` を経由して、開始済みScene RenderPass内で `vkCmdClearAttachments` を記録します。Clear対象はSwapChain Color Attachment全体です。Viewport/Scissorには制限されません。Frame外・Submit後・null色指定では `false` を返し、空実装で成功扱いしません。

`VulkanSceneContext::SetClearColor` は従来どおり**次のBeginFrameで使用するLoadOp Clear色**を設定します。描画途中のClearとは区別してください。Depth Attachmentは未実装のため、OpenGLのColor + Depth Clearと完全に等価ではありません。通常SceneへのBackend切替、Texture/Uniform、Legacy `RHICommandList` 接続は今回行いません。OpenGL実装は変更していません。

検証項目：Vulkan Scene Triangleの従来描画、BeginFrame後のClearColor→DrawIndexed、DrawIndexed後のClearColor、Frame外のClearColor拒否、Resize/最小化復帰、Validation Layer警告、OpenGL Sceneの回帰。実機ビルド・実行は未確認です。
