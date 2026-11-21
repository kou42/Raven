# Raven プロジェクト整理 M0 検証基準

比較元は `971ecfb8`。この記録のソース構成は、空の `Raven/sample.cpp` と `Raven/sample.h` を削除した後のもの。両ファイルを直接参照する include やプロジェクト・フィルター項目はなく、コンパイル対象からは既存のワイルドカードによって外れる。ソリューションとビルド設定は現行のまま評価した。

## ソースと所有

| 項目 | ロードマップ作成時 | M0 | 差 |
|---|---:|---:|---:|
| Raven の C++ ソース | 440 | 454 | +14 |
| Raven のヘッダー | 521 | 533 | +12 |
| Raven の `.inl` | 23 | 23 | 0 |
| 合計 | 984 | 1,010 | +26 |
| 機能数 | 17 | 17 | 0 |
| 機能をまたぐ直接 include | 1,039 | 1,055 | +16 |

C++ ソースとヘッダーの差にはサンプルの削除を含む。`structure.txt` の Raven 配下は現存する Git 管理対象の1,084ファイル・110ディレクトリに合わせた。ローカルの Browser Debug 生成物は集計から除いた。

直接 include は現行の `.cpp`、`.h`、`.inl` 内の `#include "Raven/機能名/..."` に相当する記述を集計した。Math と Debug は単独で、残る15機能は引き続き同じ強連結成分に属する。Tests・Benchmarks を include の集計元から外すと、Editor を除く14機能の循環が残る。条件付きコンパイルによる有効性とリンク依存は別途確認が必要。

[構成別ソース一覧](RAVEN_PROJECT_ORGANIZATION_M0_SOURCES.csv) は MSBuild の評価済み `ClCompile` 項目が基準。1はコンパイル対象、0は対象外。現行の所有プロジェクトは Root で、ビルド設定としては同じプロジェクト内で入口を切り替える。一覧は製品コード363本、テスト81本、デモ14本、ベンチマーク5本、アプリ入口と外部コード各1本。`role` はファイルの主な用途であり、分割後の所有先を確定するものではない。たとえば `RunGPUGridFluidDemo.cpp` はデモとGPU検証の入口を兼ね、Coreのコンテナベンチマークには `main.cpp` から呼ぶヘッダー実装がある。

`SceneStorageTests.cpp` と `TransformMathTests.cpp` は `test-windows-scene.ps1` が個別にコンパイルする。`VulkanEntityPickingGPUIntegrationTests.cpp` は [独立GPUテストの手順](../../Root/Tests/README.md) で実行する。この3件も一覧に含めている。

## 構成

| 構成 | 入口と追加ソース | コンパイル対象 | 実行ファイル |
|---|---|---:|---|
| 通常 | `Root/Root/main.cpp` | 456 | `Root.exe` |
| CPU UI | `UITextReflowTests.cpp`、`UIPerformanceTests.cpp` | 457 | `RavenUITests.exe` |
| GPU | `UIFontTextureGPUIntegrationTests.cpp`、OpenGL Compute、Dense SDF、Sparse SDFの統合テスト | 459 | `RavenUIGPUTests.exe` |

全評価構成に Raven の454本と `src/glad.c` が含まれる。x64 の Debug／Release で3種類の入口と割り当て計測の有無を評価した。Win32 は Debug／Release の通常構成を評価した。ソリューション名は `x86`、プロジェクト名は `Win32`。同梱の `glfw3.lib` は `dumpbin /headers` で x64（8664）のみを確認した。Win32 のリンク設定には x64 構成の Vulkan・DirectX ライブラリ指定もない。

- Debug は `_DEBUG`、Debug CRT、通常起動時の自己テストを有効にする。既定で一部のPhysicsカーネルなどを最適化する。Release は `NDEBUG` とリンク時最適化を使用する。名前付きのテスト引数には Debug 条件で実装が切り替わるものがあるため、終了コードと成功マーカーを確認する。
- `RavenUITest=true` と `RavenUIGPUTest=true` は排他的に使用する。通常の中間ディレクトリとは別に `UITests`、`UIGPUTests` を使用する。
- `RavenUIAllocationProfiling=true` は対象ソース一覧を維持し、`RAVEN_ENABLE_ALLOCATION_MEASUREMENT=1` でグローバル `new/delete` の計測を有効にする。中間出力は構成別の `AllocationProfile/<TargetName>/`、実行ファイルは `Root/x64/<Configuration>/AllocationProfile/` に置く。
- `.vcxproj.user` がある環境では F5 の引数が変わる場合がある。今回確認したローカル設定は Debug が `--ui-migration-test`、Release が `--clear-vulkan`。引数なしの起動を確認するときは実行ファイルを直接起動する。

## 依存と再現手順

- MSVC v143、C++23、Windows 10/11 SDK、GLFW 3.4の x64 静的ライブラリと native header、同梱の GLAD・stb、Vulkan SDK のヘッダー、`vulkan-1.lib`、`glslc`、DXC が必要。x64 アプリは Windows SDK の `d3d12.lib`、`dxgi.lib`、`d3dcompiler.lib` と `ws2_32.lib` をリンクする。
- 今回の環境は MSBuild 18.11.1、Vulkan SDK 1.4.357.0。ローカルの依存を利用するため既存スクリプトには `-SkipBootstrap` を指定した。bootstrap 側の Vulkan SDK インストール指定は 1.4.363.0。
- Vulkan の SPIR-V 6件は全構成、DX12 の DXIL 6件は x64 の通常アプリと割り当て計測アプリで、出力不足または入力更新時に生成する。生成先は `Raven/Assets/Shaders`。Diagnostics の生成済み定義も全構成で `ClCompile` 前に `-Check` する。
- 引数なしの通常起動は OpenGL。作業ディレクトリを `Root/Root` にし、`Raven/Assets` のシェーダー、テクスチャ、デモ用モデル等が参照できる状態で実行する。Windows の通常UIは Meiryo または Segoe UI を利用する。GPUテストは OpenGL コンテキストを作成できるデスクトップとドライバーが必要。フォントを必須にするには `-RequireFontAtlas` を指定し、必要に応じて `RAVEN_UI_TEST_FONT` に TTF/TTC を設定する。DX12／Vulkan の追加検証にはそれぞれ対応するドライバーと生成済みシェーダーを用意する。

リポジトリのルートから、`Debug` を `Release` に置き換えて再現できる。`msbuild` は Developer PowerShell から使用する。

```powershell
./scripts/bootstrap-windows-build.ps1
./scripts/build-windows.ps1 -Configuration Debug -SkipBootstrap
./scripts/test-windows-ui.ps1 -Configuration Debug -SkipBootstrap
./scripts/test-windows-gpu.ps1 -Configuration Debug -RequireFontAtlas -SkipBootstrap
./scripts/build-windows.ps1 -Configuration Debug -AllocationProfiling -SkipBootstrap
./scripts/test-windows-ui.ps1 -Configuration Debug -AllocationProfiling -SkipBootstrap
msbuild Root/Root.sln -p:Configuration=Debug -p:Platform=x86
msbuild Root/Root/Root.vcxproj '-p:Configuration=Debug;Platform=x64;RavenUITest=true' -getItem:ClCompile

Push-Location Root/Root
try {
    & ../x64/Debug/Root.exe --test-data-document-edit
    & ../x64/Debug/Root.exe
} finally {
    Pop-Location
}
```

既存の MSPDB 不整合を回避して再現する場合は、新しい PowerShell でコンパイラに `/Z7` を追加し、リンク時の PDB 生成とインクリメンタルリンクを止める。既定ビルドの中間ファイル混在を避けるため、構成ごとに固有の `IntDir` と `OutDir` を指定する。

```powershell
$env:_CL_ = '/Z7'
msbuild Root/Root.sln -p:Configuration=Debug -p:Platform=x64 `
    -p:RavenGenerateLinkDebugInformation=false -p:LinkIncremental=false `
    "-p:IntDir=$PWD\artifacts\pm0\app-debug\obj\" "-p:OutDir=$PWD\artifacts\pm0\app-debug\bin\"
```

GPUの既定入口はテクスチャ転送、フォントアトラス、補助ウィンドウを検証する。Compute は個別の引数で実行する。DX12 と Vulkan の Compute には [GPU Compute 検証用シェーダーの生成手順](../proceed/RHI_COMPUTE_SDF_ROADMAP.md#phase-04--directx-12--vulkan-compute) にある追加の SPIR-V・DXIL が必要。

## 2026-11-21の結果

| 検証 | Debug x64 | Release x64 |
|---|---|---|
| 通常アプリのビルドと起動 | ビルド0、OpenGLウィンドウ表示・正常終了 | ビルド0、OpenGLウィンドウ表示・正常終了 |
| 通常アプリの代表CLI | DataDocument編集、Scene Runtime、Stringが成功 | DataDocument編集、Scene Runtimeが成功。StringはDebug専用で終了1 |
| CPU UI | ビルド・テスト0 | ビルド・テスト0 |
| GPU | ビルド0、既定GPUテストと3バックエンドのComputeが成功 | ビルド0、既定GPUテストと3バックエンドのComputeが成功 |
| 割り当て計測付き通常アプリ | ビルド0、DataDocument編集・Scene Runtime・Stringが成功 | ビルド0、DataDocument編集・Scene Runtimeが成功。StringはDebug専用で終了1 |
| 割り当て計測付き CPU UI | ビルド・テスト0 | ビルド・テスト0 |
| 通常アプリの変更なし増分ビルド | 終了0、実行ファイルの更新なし | 終了0、実行ファイルの更新なし |

ここで 0 は終了コード0。表のビルドは `/Z7` と構成別出力を使った再検証。既定のPDB付き通常ビルドは Debug で `LNK1101`、Release で `C1902` により失敗した。CPU UI と割り当て計測の初回も `C1902` が発生した。既存の中間出力を再利用した Release の回避試行では `LNK2001` が発生し、構成別の空の中間出力を使った再ビルドで解消した。再検証には既存の型変換 `C4244`、Debug CRTの `LNK4098`、`/Z7` が `/Zi` 等に優先する `D9025` の警告がある。

Debug の通常起動は最初の終了待ち20秒でタイムアウトした。ウィンドウ表示後の初期化待ちと終了待ちを延長して再実行し、Terrain GLBの読み込みと終了コード0を確認した。Release は引数一覧に `--test-string` を表示するが、実際の分岐は `_DEBUG` 内にあり、Release では未対応引数になる。

GPUの初回スクリプトはサンドボックス内の CIM 照会で停止した。権限を切り替え、RTX 3080（32.0.16.1060）でビルド済み実行ファイルを直接実行した。既定GPUテストは実フォントを指定した。Debug の DX12／Vulkan Compute は初回に検証用シェーダーがなく失敗し、追加生成後の再試行で成功した。各Computeは代表スモークで、Validation Layer の全項目は対象外。

CPU UI は Debug／Release ともに割り当ての専用自己テストを通過した。フレームのUIベンチマークでは `cpp_available` が通常構成で0、計測付きで1。計測付き Debug の9シナリオでは `cpp_count` の最大値が30,007、同じ Release の9シナリオでは0だった。別入口 `--measure-ui-widgets` では両構成が終了0となり、プールなしの100ボタン生成で Debug 300回、Release 100回を記録した。移行後は自己テストと各ワークロードのカウンターを比較する。

`scripts/test-windows-structured-data.ps1 -AllocationTests` は、サンドボックス内ではTCPループバックが権限エラー10013で失敗した。権限を切り替えた再試行はビルド警告0、終了コード0、6,683チェック・失敗0。このテストは独立したData計測構成を使用する。

Win32 Debug は既定PDBで `LNK1101`。リンクPDBを無効化した再試行でも、x64版GLFWの `LNK4272` と未解決シンボルで終了1。Win32 Release は既定PDB付き試行を途中で止め、隔離した出力先でPDBを回避して再実行した。GLFW、DX12等の未解決シンボル `LNK1120` で終了1。M0の実行比較には x64 を使用し、Win32 のリンク失敗は既知の基準結果とする。

生ログは `artifacts/logs/` の `project-org-m0-*` と既存スクリプトが出力する `windows-*` に保存した。構成別のMSBuild評価JSONも `project-org-m0-evaluation-*` として保存した。GPUと割り当て計測を組み合わせた構成は、ソースと出力先の静的評価まで実施した。
