# UI Reflow回帰テストの実行

`Root/Tests/UITextReflowTests.cpp` は通常のアプリケーションとは別の `main()` を持ちます。
既存の `Root/Root/Root.vcxproj` に `RavenUITest=true` を指定すると、通常の `main.cpp` を除外してテストをエントリーポイントに切り替えます。
UI Coreを含む既存エンジンのリンク設定を再利用するため、テスト専用に依存ライブラリを複製しません。

## Visual Studio Developer PowerShell / Developer Command Prompt

リポジトリのルートから実行してください。

```powershell
msbuild Root/Root.sln /m /p:Configuration=Debug /p:Platform=x64 /p:RavenUITest=true
& "./Root/Root/x64/Debug/RavenUITests.exe"
if ($LASTEXITCODE -ne 0) { throw "UI回帰テストが失敗しました (exit=$LASTEXITCODE)" }
```

実行ファイルの配置先はローカルのMSBuildプロパティや既存の `OutDir` 設定によって変わる場合があります。
その場合はビルドログの出力先にある `RavenUITests.exe` を実行してください。
成功時は終了コード0、期待値と実測値が異なる場合は標準エラーへ差分を出力して終了コード1を返します。

Releaseでも検証できます。

```powershell
msbuild Root/Root.sln /m /p:Configuration=Release /p:Platform=x64 /p:RavenUITest=true
```

通常アプリケーションへ戻すときは `/p:RavenUITest=true` を付けずにビルドします。
テストモードの中間生成物は `$(Platform)\$(Configuration)\UITests\` に分離されます。

**注意:** このモードは既存エンジン全体をビルドするため、通常ビルドと同じVisual Studio C++ツールセット、submodule、Vulkan SDKなどの依存関係が必要です。CIや実機での成功は未確認です。

## OpenGL実GPU Texture転送の統合テスト

`UIFontTextureGPUIntegrationTests.cpp` はGLFWの非表示WindowにOpenGL 3.3 Core Contextを作り、
GLAD初期化後に診断付きTexture生成・GPU readback・不正サイズ・既存GL errorの検出を確認します。
通常のUI Reflowテストとは別のエントリーポイントです。GPU/Displayが使える環境で実行してください。

```powershell
msbuild Root/Root.sln /m /p:Configuration=Debug /p:Platform=x64 /p:RavenUIGPUTest=true
& "./Root/Root/x64/Debug/RavenUIGPUTests.exe"
if ($LASTEXITCODE -ne 0) { throw "GPU Texture統合テストが失敗しました (exit=$LASTEXITCODE)" }
```

実行ファイルの配置先は既存の`OutDir`設定によって変わる場合があります。
Font Atlasの実GPU生成も検証する場合は`RAVEN_UI_TEST_FONT`に実在するTTF/TTCを指定してください。
未指定時はFont Atlas部分のみ明示的にスキップします。

```powershell
$env:RAVEN_UI_TEST_FONT = "C:/Windows/Fonts/arial.ttf"
& "./Root/Root/x64/Debug/RavenUIGPUTests.exe"
```

`RavenUITest=true`と`RavenUIGPUTest=true`は同時に指定しないでください。
GPU統合テストの中間生成物は`$(Platform)\\$(Configuration)\\UIGPUTests\\`へ分離されます。

## Phase 7: 性能記録Panelの回帰テスト

`TestStatisticsRecordingPanelLifecycle`は、開始・停止・保存ボタンを実際のKeyboard入力経路で操作し、
非表示中のSnapshot集計、別UIContextへのContent移譲・元Contextへの復帰、CSV内容、
Detach後の記録リセットを確認します。保存先はテストごとの一時ディレクトリへ隔離します。
このテストはGPU・Font・OS補助Windowを生成しません。

Windows Build CIではDebugとReleaseそれぞれで通常ビルドとこのCPUテスト全体を実行します。
ログは`windows-debug-x64-logs` / `windows-release-x64-logs`へ成否にかかわらず保存します。
実機でのTab切り離し・Close復帰と、Backend/DPI別Baseline採取は`CODEX_CLOUD.md`の手順で別途確認します。

## Vulkan Entity Pickingの独立GPU統合テスト

`VulkanEntityPickingGPUIntegrationTests.cpp`はSwapChain・表示Windowを作らず、実Vulkan Driverを使います。
通常アプリやOpenGL GPUテストとは別の`main()`を持ち、Visual Studioプロジェクトでは非コンパイル項目として登録しています。

確認内容:

- Game ViewがPicking Pass・Readback Slotなしで生成できることと、Picking生成失敗時のResource回収。
- R32_SINT背景値`-1`、通常Color PassのDepthをLOADした遮蔽、TopLeftの非対称Pixel位置。
- 重なったGeometryの最前面EntityIndex、完了前の非公開、2 Frame SlotのReadback、古いSequence破棄。
- Resizeに相当するTarget再生成時の未消費結果リセットと、Validation Errorが0件であること。

WindowsではVulkan SDKとVisual Studio Developer PowerShellを使用し、リポジトリルートで次を実行します。
通常アプリのMSBuildでもShaderは生成されますが、この独立テストでは必要な2 Shaderだけを明示的に生成します。

```powershell
$repo = (Resolve-Path .).Path
$testOutput = Join-Path $repo "artifacts/vulkan-picking"
New-Item -ItemType Directory -Force -Path $testOutput | Out-Null
& "$env:VULKAN_SDK/Bin/glslc.exe" Root/Root/Raven/Assets/Shaders/Vulkan/SceneTriangle.vert -o Root/Root/Raven/Assets/Shaders/Vulkan/SceneTriangle.vert.spv
if ($LASTEXITCODE -ne 0) { throw "Vertex Shader compile failed" }
& "$env:VULKAN_SDK/Bin/glslc.exe" Root/Root/Raven/Assets/Shaders/Vulkan/EntityPicking.frag -o Root/Root/Raven/Assets/Shaders/Vulkan/EntityPicking.frag.spv
if ($LASTEXITCODE -ne 0) { throw "Picking Shader compile failed" }
$sources = @(
    "Root/Tests/VulkanEntityPickingGPUIntegrationTests.cpp",
    "Root/Root/Raven/Platform/Vulkan/VulkanDevice.cpp",
    "Root/Root/Raven/Platform/Vulkan/VulkanPhysicalDevice.cpp",
    "Root/Root/Raven/Platform/Vulkan/VulkanGraphicsPipeline.cpp",
    "Root/Root/Raven/Platform/Vulkan/VulkanOffscreenRenderTarget.cpp",
    "Root/Root/Raven/Platform/Vulkan/VulkanSceneRenderTarget.cpp",
    "Root/Root/Raven/Platform/Vulkan/VulkanSceneRHITexture.cpp",
    "Root/Root/Raven/Platform/Vulkan/VulkanSceneTexture.cpp",
    "Root/Root/Raven/Platform/Vulkan/VulkanSceneBuffer.cpp",
    "Root/Root/Raven/Renderer/RHI/RHIOffscreenRenderTarget.cpp"
)
& cl.exe /nologo /std:c++17 /EHsc /DGLFW_INCLUDE_NONE `
    "/I$repo/Root/Root" "/I$repo/Root/Root/include" "/I$env:VULKAN_SDK/Include" `
    "/Fo$testOutput/" "/Fe$testOutput/VulkanPickingTests.exe" $sources `
    /link "/LIBPATH:$env:VULKAN_SDK/Lib" vulkan-1.lib
if ($LASTEXITCODE -ne 0) { throw "Vulkan Picking test build failed" }
# SDKをcopy-only配置している場合もValidation Layerを見つけられるようにします。
$env:VK_LAYER_PATH = "$env:VULKAN_SDK/Bin"
$env:RAVEN_VULKAN_REQUIRE_VALIDATION = "1"
$env:VK_LAYER_ENABLES = "VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT"
Push-Location "$repo/Root/Root"
try {
    & "$testOutput/VulkanPickingTests.exe"
    if ($LASTEXITCODE -ne 0) { throw "Vulkan Picking GPU integration failed" }
} finally {
    Pop-Location
}
```

Linuxでの確認はMesa llvmpipeとKhronos Validation Layer（同期検証有効）で成功しました。
GCCでは既存Base.hの無効コード内の矢印文字を除いた一時ヘッダーと、既存型/メンバー同名宣言用の
`-fpermissive`を使用しています。Windowsの正式ビルド・Editor実操作は別途必要です。
この独立テストは既存Windows GPU workflowからは自動実行されません。
