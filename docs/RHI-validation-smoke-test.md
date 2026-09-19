# RHI Validation / Debug Layer 検証手順

## 前提

- Windows、Visual Studio 2022（MSVC v143）、Windows SDK
- Vulkan SDK（`VULKAN_SDK` 環境変数）、GLFWなど既存Raven依存ライブラリ
- DX12 Debug Layer / GPU-Based Validationを試す場合はWindowsの「Graphics Tools」
- `Root/Root/Root.vcxproj` のx64構成は `vulkan-1.lib`、`d3d12.lib`、`dxgi.lib` をリンクする

## ビルド

Visual Studioで`Root/Root.sln`を開き、Debug|x64とRelease|x64をそれぞれビルドする。
ソリューション名が異なる場合は`Root/Root/Root.vcxproj`を直接開く。
本PRはGitHub上でコードを変更しており、Windowsビルド・GPU実行の成功をまだ確認していない。

## 実行

実行ファイルを`Root/Root`を作業ディレクトリとして起動する。
`RAVEN_RHI_SMOKE_FRAMES`に正の整数を設定すると、指定フレーム数の描画成功後に終了する。
未設定・0・不正値では通常の対話操作となる。描画またはResize失敗時は終了コード1、正常終了は0。

PowerShell例（実行ファイルの場所はビルド出力に合わせて変更）：

```powershell
$env:RAVEN_RHI_SMOKE_FRAMES = "120"
$env:RAVEN_RHI_VALIDATION = "1"
$env:RAVEN_RHI_VERBOSE = "0"
& ".\x64\Debug\Root.exe" --clear-vulkan
$LASTEXITCODE
& ".\x64\Debug\Root.exe" --clear-dx12
$LASTEXITCODE
```

GPU-Based Validationの追加確認時だけ`$env:RAVEN_DX12_GPU_VALIDATION = "1"`を設定する。
`RAVEN_RHI_VALIDATION=0`では両APIの診断Layerを無効化して描画できるか確認する。
Releaseでは環境変数にかかわらず診断を無効化する。

## 手動検証マトリクス

| 対象 | 操作 | 確認事項 |
| --- | --- | --- |
| Vulkan Debug | 起動→120フレーム→終了 | Validation警告/エラー、終了コード |
| DX12 Debug | 起動→120フレーム→終了 | InfoQueue警告/エラー、終了時Live Object Report |
| Vulkan/DX12 Debug | Windowサイズ変更→最小化→復帰→終了 | SwapChain再生成、Resizeエラー、残存Resource |
| Vulkan/DX12 Debug | `RAVEN_RHI_VERBOSE=1` | 詳細ログ出力 |
| Vulkan/DX12 Debug | `RAVEN_RHI_VALIDATION=0` | Debug runtimeなしで起動できるか |
| DX12 Debug | GPU Validation有効で起動→描画→終了 | GPU側エラー、処理負荷 |
| Vulkan/DX12 Release | 起動→描画→終了 | Debug機能への依存がないこと |

`RAVEN_RHI_SMOKE_FRAMES`による自動終了だけではResize・最小化復帰は検証できないため、別途手動操作する。
DX12 Live Object ReportはVisual StudioのDebug Outputにも出力される場合がある。
Reportが出ない場合はGraphics ToolsとDebug Layerの有効化を確認する。
実行時ログを保存し、警告が既知のものか、新規回帰かを区別する。

## 実機で確認されたDebug Smoke Test（2026-09-20）

- DX12 / VulkanともにRTX 3080で120フレーム描画し、終了コード0を確認。
- DX12 Debug LayerおよびVulkan `VK_LAYER_KHRONOS_validation`の有効化を確認。
- DX12の`Live ID3D12Device ..., Refcount: 2`はINFO（Severity 2）。Report呼び出し時点でDeviceを所有しているため、この表示だけではリークと判定しない。子オブジェクトのLive報告やWARNING/ERRORがないか別途確認する。
- Vulkan LoaderからEOS Overlay Layer重複のWARNINGが出たが、RavenのVulkan APIに対するValidationエラーは記録されていない。
- Resize・最小化復帰・Releaseは、このログでは未検証。`RAVEN_RHI_SMOKE_FRAMES`を未設定にして手動確認する。
