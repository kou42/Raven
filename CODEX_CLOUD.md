# Raven Codex Cloud 運用ガイド

更新日: 2026-11-08

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

- Visual Studio 2026 / MSVC v145 / C++23 / Windows SDK 10.0による `Root/Root.sln` のビルド
- Vulkan SDKの `glslc.exe` と `vulkan-1.lib` を使う検証
- DirectX 12ライブラリとDXCを使う通常x64ビルド
- CPU UI回帰テストとGPU統合テストの実行

Codex CloudはLinuxコンテナであるため、Windows検証をCloud内で成功したものとして扱いません。

## 開発環境の基準と移行状況

開発環境の基準はVisual Studio 2026、MSVC v145、C++23です。
プロジェクトの `PlatformToolset` は `v145`、`LanguageStandard` は `stdcpp23` を基準として確認します。
ビルド結果を報告するときは、IDEの名称だけでなく、実際に使用したMSVC toolsetとC++言語標準も記録してください。

本更新は運用ガイドのみを対象とします。確認時点の `Root/Root/Root.vcxproj` には `v143` 指定が残り、
`scripts/check-codex-cloud-baseline.sh` も `v143` を検査しています。
Windows CIの `.github/workflows/windows-build.yml` は `windows-2022` を使用し、ビルドスクリプトにも旧環境の診断文言が残っています。
これらの設定・スクリプト・CIは別途移行と検証が必要です。現在のコマンドの実行成功だけで、
VS2026 / MSVC v145 / C++23の組み合わせを検証できたとは扱わないでください。

## Windowsビルドの再現

Visual Studio 2026（MSVC v145とWindows 10/11 SDKを含む）が必要です。新しいチェックアウトでは次を実行します。

```powershell
git submodule update --init --recursive
./scripts/bootstrap-windows-build.ps1 -InstallVulkanSdk
./scripts/build-windows.ps1 -Configuration Debug -Platform x64 -SkipBootstrap
```

ブートストラップはGLFW 3.4、Vulkan SDK 1.4.363.0、同版のVulkan Runtime Componentsを公式配布元から取得し、SHA-256を検証します。Vulkan SDKはレジストリを変更しないcopy-only方式で配置し、Runtime Componentsの`vulkan-1.dll`はCI上でCPUテスト実行ファイルをロードするためだけにPATHへ追加します。既存のVulkan SDKとシステムVulkan Runtimeを使う場合は `-InstallVulkanSdk` を外し、`VULKAN_SDK` を設定してください。

GitHub Actionsの `.github/workflows/windows-build.yml` は、Pull RequestとmasterへのpushでDebug / Release x64の通常ビルドとCPU UI回帰テストをそれぞれ実行し、GPU統合テスト実行ファイルも別ジョブでビルドします。通常ビルドとCPU UI回帰テストのMSBuildログ・テスト出力は、成否にかかわらず構成別のartifactへ7日間保存します。

## GPU統合テスト

GPU統合テストはOpenGL 3.3 Core Contextと表示セッションを必要とするため、標準のGitHub hosted runnerでは実行しません。`.github/workflows/windows-gpu-tests.yml` をActions画面から手動実行し、次の条件を満たすWindows self-hosted runnerを使用します。

- runner labels: `self-hosted`, `Windows`, `X64`, `raven-gpu`
- ベンダー製GPUドライバーとOpenGL 3.3 Core以上
- ログイン中の対話型Windowsデスクトップ（runnerをWindows Serviceや切断済みRDP Sessionで動かさない）
- Visual Studio 2026、MSVC v145、C++23、Windows 10/11 SDK
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


## Phase 7: UI性能記録の検証

`windows-build.yml`の`Debug x64`と`Release x64`は、それぞれ通常アプリのビルド後に
`RavenUITests.exe`をビルド・実行します。GPU用ジョブは引き続きビルドのみです。
Release追加後のCI結果とEditor実操作は、実行結果が得られるまで未確認として扱います。

CPUテストには、Statisticsの実際の記録ボタンへEnterを配送し、以下を確認する回帰テストを含みます。

- 開始、記録中の保存拒否、停止、停止後のCSV内容、再開始。
- 非表示にしたPanelへのSnapshot供給時も集計を継続すること。
- 同じPanel Contentを別UIContextへ移しても記録が残り、移動先で停止・保存できること。
- 元Contextへ戻した後の再開始と、Detach / 再Attach時の初期化。

このCPUテストはOS補助Window生成やEditorLayerからのSnapshot供給を実行しません。
Windows実機では、同じBackend・構成・DPI・Scene・Panel配置・操作条件に揃え、次を確認します。

1. 通常Editorを起動し、Statistics末尾のStart / Restartから記録する。
2. View MenuでStatisticsを非表示にしてから再表示し、Samplesが増えていることを確認する。
3. 記録中にStatistics Tabを補助Windowへ切り離し、Closeして元Paneへ戻す。
4. Stop後のSamplesが止まり、Save CSVで作業ディレクトリに記録が生成されることを確認する。
5. CSVの全10項目にsamples / average / maxがあり、Restart後は新しい集計になることを確認する。
6. Debug / Release、OpenGL / DX12 / Vulkan、100% / 高DPIについて結果とCSVを残す。

CSVのscopeは常にMain Window UIです。Statisticsを補助Windowに移しても補助Window自身の
描画性能へ切り替わりません。異なる条件のCSVを同一Baselineとして比較しないでください。

## Phase 7: Window別性能記録とAllocation計測モード

Statistics末尾には、同じApplication Frameで実際に描画完了したWindowごとの値とBuild時間合計を表示します。
記録テーブルは全描画Windowの合計、CSVは`all_windows_ui`と`window_<WindowID>`を区別します。
最小化等で描画を行わなかったWindowへ0値や前回の値を補充しません。Close前の記録はStop/Restartまで残ります。
Window IDは再利用しません。1セッションのWindow別履歴は64件まで保持し、超過したWindowサンプル数は
`recording,omitted_window_samples`行とPanelの警告へ記録します。全Window合計には超過分も含みます。
最初の10項目は既存のBuild/DrawList/Element指標、追加2項目はC++ Allocationの件数・要求bytesです。
CSVの`samples`は項目ごとの実測サンプル数です。未計測項目は`samples=0,average=N/A,max=N/A`となります。
全WindowのElement Peakは、Application生存期間中の「同一Frameで描画完了したWindowのElement数合計」の最大です。
Window別Peakは各UIContext生存期間の最大です。

C++ Allocation計測は通常ビルドでは無効です。`-AllocationProfiling`または
`/p:RavenUIAllocationProfiling=true`で計測用ビルドを作ると、Global new/deleteを置換します。
Thread-localな計測区間だけ成功した確保件数と要求bytesを加算し、追跡Mapや追加Allocationは使いません。
カウンター自体のためにmutexを取らず、同じ計測先の入れ子も二重加算しません。

対象区間はUIContext BeginFrame（DPI同期を含む）、MainのOnRavenUIRender更新・Font更新・Overlay、
EndFrame内のLayout/DrawList生成・同期Renderer処理、補助WindowのFont更新・EndFrameです。
MainのLayerによるPanel更新は、Panelを補助Windowへ移動してもMainの更新コストとして数えます。
DrawList外のSTL文字列、Layout/Table等の一時領域と、これらの区間中の保持領域拡張も含みます。
これは一時領域だけの生存量やPeak Heapではなく、区間内のC++確保要求の累計です。
DrawList既存Allocation指標とは重複するため、両者を足して総Allocationとはしないでください。

`malloc`/OS/GPU Driver、別DLL内独自Allocator、別Thread、入力Callback、起動時のWidget生成、
Explicit BackendでEndFrameの外にある描画準備・Submit処理は、このカウンターの対象外です。
GPUメモリや全Process Heapを測定した値として扱いません。計測自身の影響があるため、
Baseline比較では同じ計測フラグ・Backend・構成・DPI・Scene・Panel表示・操作条件を使用します。

既存の「CSVはMain Windowのみ」という説明は初回実装時の状態です。本追加実装により、
通常Editorが渡す完了Window Batchがある場合は全描画Windowの記録へ拡張されています。
