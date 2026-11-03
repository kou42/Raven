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

DX12補助WindowはGPUテスト実行ファイルを`--dx12-auxiliary`で起動して検証します。
`Root/Root`を作業Directoryにし、Scene/UIのDXIL ShaderとDX12対応GPUが必要です。

```powershell
& "../x64/Debug/RavenUIGPUTests.exe" --dx12-auxiliary
```

ビルドは上記の`RavenUIGPUTest=true`を使用します（実際のOutDirに合わせてPathを調整）。
このモードはOpenGLを初期化せず、実OS Windowを表示します。共有Deviceの同一性、
Main Context終了後の共有Device生存、補助SwapChainの描画・Resize・3回の解放、
重複登録拒否、終了後の描画拒否、Runtime Shutdown時の補助解放を確認します。
DX12のpixel readbackは含みません。実EditorのScene/Game Texture表示と入力操作は別途実機で確認します。
Debug実行ではログのDX12 Debug Layer Warning/Errorも確認してください。

Vulkanは同じGPUテスト実行ファイルに`--vulkan-auxiliary`を指定します。
`Root/Root`を作業Directoryにし、Vulkan Loader/対応GPUとScene/UIのSPIR-Vが必要です。

```powershell
$env:RAVEN_RHI_VALIDATION = "1"
& "../x64/Debug/RavenUIGPUTests.exe" --vulkan-auxiliary
```

Main/補助のDevice・Queue同一性、Main Context終了後の共有Instance生存、Mainが書く
Viewport Textureを補助2 WindowでSamplingする経路、3回のClose、補助Resize、
Main RenderPass/Texture交換後のDescriptor再準備を検証します。
GPU pixel readbackは含みません。DebugではValidation Layerが利用可能であることと、
ログにWarning/Errorがないことも確認してください。
再生成待ちは`ResizeRequired`として再試行し、完成Frameの件数へ計上せず、Closeしません。

`TestEditorDetachedLayoutSave`は実DockからScene/Game/Inspectorを抽出し、保存用Snapshotの
順序・選択・非表示Panel・重複防止、元Pane消失時の既定Pane/先頭Paneへの復元を確認します。
一時ファイルへの保存・読み込みとDock再生成、不正入力時の出力維持も検証します。
実行中のDockと切り離しContentの所有権が変化しないことを確認し、OS補助WindowやGPUは生成しません。

`TestStatisticsRecordingPanelLifecycle`は、開始・停止・保存ボタンを実際のKeyboard入力経路で操作し、
非表示中のSnapshot集計、別UIContextへのContent移譲・元Contextへの復帰、CSV内容、
Detach後の記録リセットを確認します。保存先はテストごとの一時ディレクトリへ隔離します。
このテストはGPU・Font・OS補助Windowを生成しません。

`TestWindowUIKeyRouting`はApplicationの共通キー配送をGPU/Window生成なしで通し、
補助ContextのClipboard、共有履歴、前面Layer優先と処理済み入力の配送停止、
Text/Popup/Mouse Capture/Drag/Gizmo/Repeat/Key Releaseの拒否を検証します。
MainのFocusを補助Contextの操作へ流用しないことも確認します。

Windowsでは末尾の`TestWindowsMouseMessageCoordinates`がNo-APIのOS Windowを一つ生成します。
OpenGL Context・SwapChain・GPUは使用せず、Windowは直ちに非表示にします。
実際のWndProc/GLFW callbackへqueue済みDown/Move/Upと縦横Wheelを配送し、
現在のCursor位置に依存しない座標保持、Screen→Client変換、負のClient座標を検証します。
このWindows固有テストにはOS Windowを生成できる環境が必要です。

`TestInputProviderLifetime`はMain相当の最初の生存providerを保持し、中間/先頭/末尾の
任意順破棄、補助providerの反復Close、最後の破棄後のNeutral値をGPUなしで確認します。
`TestDetachedInspectorViewport`は実InspectorのDock抽出・別Context移譲・Resize・復帰と、
Window論理座標のRoot/Page寸法および空でないClipを確認します。
`TestInspectorCommitHistoryRouting`には移譲後の不完全入力/PreviewをSnapshot同期で壊さず、
Enter/Focus喪失が一度だけCommandを確定し、移動先/復帰先でUndo/Redoできる回帰も含みます。

GPU統合の`TestDetachedUIRendering`は共有OpenGL Contextを3回生成・破棄し、
Stretch/Clip付きDock本文を移譲してpixel readbackで可視描画を確認します。
補助WindowのResizeと元Dock復帰後も採取し、GL errorなし・Core ProfileのPolygon Mode復元を確認します。
default framebufferを検証するため小さいテストWindowを表示します。実GPUとOS Displayが必要です。

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

## Phase 7: 複数WindowとC++ Allocation計測

Vulkan SDR出力の追加検証（先に `test-windows-gpu.ps1` で同構成のGPUテストをビルド）:

```powershell
Push-Location Root/Root
../x64/Debug/RavenUIGPUTests.exe --vulkan-surface-formats
../x64/Debug/RavenUIGPUTests.exe --vulkan-color
Pop-Location
```

`--vulkan-surface-formats` はWindow不要のCPU形式選択・transfer clear変換検証。
`--vulkan-color` はCPU検証に加え、Main UNORMと補助Windowの強制SRGB Surface/UNORM View、
Resize後のclear/opaque/半透明Blend/Viewport画像をGPU readbackし、各channelの誤差2/255以内を確認する。
同一Deviceのmutable swapchain拡張と依存拡張、BGRA SRGB Surface、TRANSFER_SRC対応が必要で、
条件不足をskip成功にはしない。通常表示ではUNORMがあればmutableは不要で、readbackも必須ではない。
Debug/Release双方で実施する。Vulkanの数値色検証であり、DX12/OpenGLの数値色readbackは追加していない。

`UIPerformanceTests.cpp`をCPU UIテストへ追加しました。完了Windowの集計、独立したContext/Applicationの
Frame連番、重複排除、最小化/Close後の古い値の除外、CloseしたWindowの履歴保持、Window IDの追加、
Window別履歴の上限、項目別の未計測値、CSVのScope・サンプル数・N/A、再開始を検証します。
通常モードとAllocation計測モードの両方で、入れ子・例外・Thread分離、Aligned / Sized / Nothrow / 0 byte、
new_handler失敗、およびSTL一時領域の確保を検証します。

通常のDebug/Releaseテストは従来どおりです。Allocation計測を有効にする場合:

```powershell
./scripts/test-windows-ui.ps1 -Configuration Debug -Platform x64 -AllocationProfiling
./scripts/build-windows.ps1 -Configuration Release -Platform x64 -AllocationProfiling
```

MSBuildを直接使う場合は`/p:RavenUIAllocationProfiling=true`です。計測用の中間生成物と実行ファイルは
`AllocationProfile`配下へ分離します。通常ビルドはGlobal new/deleteを置換しません。
Windows CIの`UI allocation profiling x64`ジョブでも、計測モードのCPU UIテストを実行します。
このジョブとWindows/MSVCでの実行結果は、結果が得られるまで未確認です。

Linuxで同じテストを独立実行する場合は、`RAVEN_UI_PERFORMANCE_TEST_MAIN`を定義し、
`UIPerformanceTests.cpp`、`AllocationMeasurement.cpp`、`StatisticsActions.cpp`、`StatisticsUISnapshot.cpp`、
`CPUProfiler.cpp`をリンクします。計測モードには`RAVEN_ENABLE_ALLOCATION_MEASUREMENT=1`を追加します。
Threadを使用するため`-pthread`が必要です。既存Base.hの無効コード内の矢印文字はGCC向けの一時ヘッダーで
除いて確認しており、これをWindows正式ビルドの合格とは扱いません。
