# Raven Codex Cloud 運用ガイド

更新日: 2026-10-29

## 目的

RavenのCodex指示型開発を、ユーザー承認を必須とするマイルストーン方式で進めます。
Codex Cloudは調査、編集、静的検査、差分整理を担当し、Windows固有の正式ビルドと実行テストはWindows環境またはWindows CIで行います。

## Cloud環境のInstall script

```bash
set -euo pipefail
git submodule sync --recursive
git submodule update --init --recursive
test -f AGENTS.md
test -f structure.txt
test -f Root/Root.sln
test -f Root/Root/Root.vcxproj
test -f Root/Tests/README.md
test -f Root/Tests/UITextReflowTests.cpp
test -f Root/Tests/UIFontTextureGPUIntegrationTests.cpp
test -f Root/Root/vendor/imgui/imgui.cpp
bash scripts/check-codex-cloud-baseline.sh
```

Agent実行時のインターネットアクセスは原則無効とし、Secretや追加環境変数を設定しません。

## 検証境界

### Codex Cloud

- `AGENTS.md` と `structure.txt` の確認
- 関連コード、呼び出し関係、所有権、データフローの調査
- 承認済み範囲だけの編集
- 静的ベースライン確認と差分確認
- Windowsビルド未実施の場合、その事実の明示

### Windows環境またはWindows CI

- Visual Studio 2022 / MSVC v143 / Windows SDK 10.0による `Root/Root.sln` のビルド
- Vulkan SDKの `glslc.exe` と `vulkan-1.lib` を使う検証
- DirectX 12ライブラリとDXCを使う通常x64ビルド
- CPU UI回帰テストとGPU統合テストの実行

Codex CloudはLinuxコンテナであるため、Windows検証をCloud内で成功したものとして扱いません。

## Windowsビルドの再現

Visual Studio 2022（MSVC v143とWindows 10/11 SDKを含む）が必要です。新しいチェックアウトでは次を実行します。

```powershell
git submodule update --init --recursive
./scripts/bootstrap-windows-build.ps1 -InstallVulkanSdk
./scripts/build-windows.ps1 -Configuration Debug -Platform x64 -SkipBootstrap
```

ブートストラップはGLFW 3.4、Vulkan SDK 1.4.363.0、同版のVulkan Runtime Componentsを公式配布元から取得し、SHA-256を検証します。Vulkan SDKはレジストリを変更しないcopy-only方式で配置し、Runtime Componentsの`vulkan-1.dll`はCI上でCPUテスト実行ファイルをロードするためだけにPATHへ追加します。既存のVulkan SDKとシステムVulkan Runtimeを使う場合は `-InstallVulkanSdk` を外し、`VULKAN_SDK` を設定してください。

GitHub Actionsの `.github/workflows/windows-build.yml` は、Pull RequestとmasterへのpushでDebug x64の通常ビルドとCPU UI回帰テストを実行し、GPU統合テスト実行ファイルも別ジョブでビルドします。失敗時はMSBuildログとテスト出力をartifactへ7日間保存します。

## GPU統合テスト

GPU統合テストはOpenGL 3.3 Core Contextと表示セッションを必要とするため、標準のGitHub hosted runnerでは実行しません。`.github/workflows/windows-gpu-tests.yml` をActions画面から手動実行し、次の条件を満たすWindows self-hosted runnerを使用します。

- runner labels: `self-hosted`, `Windows`, `X64`, `raven-gpu`
- ベンダー製GPUドライバーとOpenGL 3.3 Core以上
- ログイン中の対話型Windowsデスクトップ（runnerをWindows Serviceや切断済みRDP Sessionで動かさない）
- Visual Studio 2022、MSVC v143、Windows 10/11 SDK
- 公式依存物を取得できるインターネット接続

手動ワークフローはFont Atlasを含むGPU Texture upload/readbackを実行し、終了コード0、`[PASS] GPU Texture upload diagnostics`、Font Atlasの非SKIPを必須とします。ビルドログとテストログは成否にかかわらず `windows-gpu-integration-logs` artifactへ14日間保存します。

同じ環境でローカル確認する場合は次を実行します。

```powershell
git submodule update --init --recursive
./scripts/bootstrap-windows-build.ps1 -InstallVulkanSdk
./scripts/test-windows-gpu.ps1 -Configuration Debug -Platform x64 -RequireFontAtlas -SkipBootstrap
```

## 正式コマンド

```powershell
./scripts/build-windows.ps1 -Configuration Debug -Platform x64

./scripts/test-windows-ui.ps1 -Configuration Debug -Platform x64

./scripts/test-windows-gpu.ps1 -Configuration Debug -Platform x64 -RequireFontAtlas
```

Font Atlasも検証する場合は実在するTTF/TTCを `RAVEN_UI_TEST_FONT` に設定します。
`RavenUITest=true` と `RavenUIGPUTest=true` は同時指定しません。

## マイルストーン

1. M1: Cloud基準設定、承認ゲート、静的ベースライン確認
2. M2: Windowsビルドを再現可能にするCIまたは検証スクリプト
3. M3: `RavenUITests.exe` の自動実行と結果判定
4. M4: GPU統合テストの実行環境と手動確認手順
5. M5以降: ロードマップ単位の実装、テスト、最適化

各マイルストーンは、計画承認、実施、結果報告、停止の順で進めます。
承認済みマイルストーンの範囲内では、作業ブランチ作成、コミット、Pull Request作成までは追加承認なしで実行します。マージにはユーザーの明示承認が必要です。

