# Explicit Scene Frame結果の回帰テスト

`ExplicitSceneFrameSelfTests.cpp` は、Window・GPU Deviceを作らずに
`Application::HandleExplicitSceneFrameResult` のCallback呼び出し順序に加え、
Mockの`RHISceneFrameLifecycle`を使って`ExecuteExplicitSceneFrame`の
Prepare → BeginFrame(Acquire) → Drawの順序と各失敗経路、
さらに`RHISceneMeshRenderer::FinishActiveFrame`のSubmit → Present順序、
Submit失敗時のPresent抑止、両段階のResizeRequired/FatalError伝播、
Window寸法変更時のResize失敗によるSnapshot破棄を検証します。

Visual Studio Developer PowerShell / Developer Command Promptで、リポジトリのルートから実行してください。

```powershell
msbuild Root/Root.sln /m /p:Configuration=Debug /p:Platform=x64
& "./Root/Root/x64/Debug/Root.exe" --test-explicit-frame
if ($LASTEXITCODE -ne 0) { throw "Explicit Frame SelfTest failed (exit=$LASTEXITCODE)" }
```

実行ファイル名・配置先は既存のOutDir/TargetName設定に合わせて読み替えてください。
Debugの通常起動でもSelfTestを実行しますが、専用引数ならSceneやWindowを起動せず終了します。
`assert`を使用するため、テスト実行はDebug構成で行ってください。
既存エンジン全体のリンク依存（Visual Studio C++ツールセット、Vulkan SDKなど）は必要です。
このテストはGPU実機でのAcquire/Resize/Presentの成否を検証するものではありません。
`EndFrame`と`Present`の呼び出し順序はMockで検証しますが、
実Backend内部のSubmit/Present実装の成否やGPU Fenceの完了までは確認しません。

## GPU実機で確認するShutdown順序

MockはGPU Fenceやnative Resourceの解放を再現しないため、Vulkan・DX12の
Scene Demoで通常終了、Resize直後の終了、最小化からの復帰後の終了、
Submit/Present失敗後の終了をそれぞれ確認してください。

- Application: Scene::Shutdown → Renderer::Shutdown → Runtime::Shutdown。
- Vulkan Runtime: WaitIdle → Prepared Snapshot / Pipeline / Texture等の上位参照解放 → Context::Shutdown。
- Vulkan Context: WaitIdle → 外部参照が残るBuffer/Texture/Pipelineのnative handle無効化 → Frame Sync / RenderTarget / SwapChain → Device。
- DX12 Runtime: Prepared Snapshot / Pipeline / Texture等の上位参照解放 → Context::Shutdown。
- DX12 Context: Fence待機 → Frameが保持するBuffer/PSO/Texture参照解放 → RenderTarget / SwapChain → Fence / Queue → Device。

特にDX12のFence待機失敗時は、この順序だけでGPU完了を保証できません。
Device Removal等の実機エラーについてはDebug Layerと終了時ログを別途確認してください。
DX12 ContextのShutdownではFence待機失敗を標準エラーへ出力し、Debug Layerの蓄積メッセージも取得します。
これは失敗を検知するための診断であり、GPU完了を保証したりDeviceを復旧したりするものではありません。

## FatalError後のContext再利用禁止

Vulkan・DX12のScene Contextは、BeginFrame / EndFrame / PresentがFatalErrorを返した後、
同じContextでのBeginFrame・EndFrame・Present・Resizeを拒否します。
Resize内部のGPU待機・SwapChain/RenderTarget再生成失敗も同様です。
Acquire/PresentのResizeRequiredはFatalErrorとは区別し、通常のResize経路を維持します。
Shutdownで状態をリセットし、次のInitは新しいContext Resourceを作成します。

この終端状態は実GPUを使うBackend側のため、上記CPU Mockテストでは直接検証できません。
Debug実機では通常描画、Resize、最小化復帰、通常終了を確認してください。
Device RemovedやSubmit失敗の強制再現は今回の通常動作確認の対象外です。

## PR #286 初期化失敗・終了経路の確認

- Window生成に失敗した場合、Scene/Runtimeを初期化せず終了します。
- Runtime::Init失敗では、各Backendの部分初期化済みContextをShutdownします。共通入口も
  DiscardPreparedFrame → SceneのOnBeforeShutdown → Renderer::Shutdown → Runtime::Shutdown
  の順で終了します。Backend内Shutdownの重複呼び出しは冪等であることが前提です。
- Scene構築またはPrepareScene失敗時も同じ順で解放し、Windowより先にRuntimeを破棄します。
- 通常終了・Frame FatalError・Window Resize失敗時はRunnerが終了し、
  Prepared Snapshot → Scene → Renderer → Runtime → Windowの順で所有権を解放します。
- VulkanはRuntimeとContextでWaitIdleを試み、Contextが外部Refのnative Resourceを無効化します。
  DX12はContextのFence待機後にFrame保持参照を解放します。待機失敗時のGPU完了は保証しません。

確認対象の変更ファイルはApplication、Vulkan/DX12 Scene Runtime・Context、
共通RHIインターフェース、Scene Renderer、両Demo、CPU Mockテスト、mainです。
GitHub上でのソース確認はMSBuild・GPU検証や失敗注入の代わりにはなりません。
