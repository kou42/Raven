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
