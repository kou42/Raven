# UI Modernization M0: 最適化前Baseline

対象commit: a7facb20にM0計測のみを追加。Dirty/Cache/Batchingの最適化は含めない。

## 固定条件

- Windows x64 / MSVC v143 / DebugとReleaseを別実行。
- GPUなし・単一UIContext、論理/Framebuffer 1280×720、DPI X/Y=1。
- Font/Textなし、Popup/Tooltip/Dragなし。既定Theme、透明度1、回転なし。
- Widget数はUIPanelの個数。Root・Content・ScrollView・内部Scrollbarは別途ElementCountに含む。
- 全ケースで1000×160のScrollView、Contentは10列、行間隔20px、Panelは96×18px。
- static: 構築後のWidget更新なし。update: 全Panelの幅を95/96px、色を交互更新。
- scroll: Y Offsetを(frame % 20)×2pxへ変更。100 WidgetでもOverflowする条件。
- 各ケース30 FrameのWarmup後120 Frameの算術平均。生成/破棄時間は平均に含めない。
- 実行順は100→1,000→10,000、各個数でstatic→update→scroll。

## 計測境界と棚卸し

| 項目 | 対象・制限 |
|---|---|
| BuildMilliseconds | 従来どおりEndFrameのTooltip/Drag更新からUITessellator::Measure完了まで |
| LayoutMilliseconds | RootのMeasure/Reflow/Arrange。Dirty判定と時計呼び出しの小さな固定負荷を含む |
| DrawListMilliseconds | BuildからLayoutとCommandMetricsを除いた時間。Overlay生成も含む |
| CommandMetricsMilliseconds | UITessellator::Measureによる描画量集計。実Geometry変換ではない |
| Geometry平均 | テストが別途UITessellator::Tessellateを実行。Frameごとの出力vector確保を含み、破棄は含めない |
| RendererMilliseconds | Render呼び出し全体のCPU時間。Geometry/Upload/Submitの合計。GPU時間ではない |
| RendererAvailable | Headlessではfalse。未測定を0ms成功として扱わない |
| Command/Vertex/Index/TextureSwitch | 既存集計。頂点/Indexはテストで実変換結果と照合 |
| FrameAllocationCount/Bytes | DrawList内部のFrame確保。C++全確保やGPUメモリを意味しない |
| RetainedAllocationBytes | DrawListが保持する容量。Widget Treeの総メモリではない |
| ElementCount/Peak | Root以下の保持Element数/ContextのPeak |
| CppAllocations | 既存の任意プロファイルビルド限定。無効時は未測定。テスト側Geometry確保は含めない |

BeginFrame・ユーザーSetter・ElementCount集計はBuildの外側。新規区間はUIFrameMetricsから参照する。
Statistics Panel/既存CSVの列は維持し、今回の詳細CSVはテスト出力へ分離する。
時計と既存Measure処理の負荷を含むため、小規模ケースの絶対時間は参考値とする。
GPU Submit単体/GPU実行時間/実画面の一致はこのHeadless計測では検証しない。

## 再実行

リポジトリルートのDeveloper PowerShellで実行する。既存SDK/ライブラリが必要。

```powershell
msbuild Root/Root.sln /m /p:Configuration=Debug /p:Platform=x64 /p:RavenUITest=true
& ./Root/x64/Debug/RavenUITests.exe *> ui-m0-debug-tests.log
# 終了コード0を確認した後、CSVを抽出
Select-String '^baseline,' ui-m0-debug-tests.log
```

ReleaseはConfigurationと実行パスをReleaseへ変更する。OutDirを変更した環境はビルドログのパスを使う。
Allocation測定は`/p:RavenUIAllocationProfiling=true`で別ビルドし、通常構成と混ぜない。
CPU回帰テスト全体の成功とBaselineの時間を別々に記録する。

## 継続検証

既存ロードマップ`roadmap/proceed/RAVEN_UI_ROADMAP.md`の未完了を維持する。
Text DemoのASCII/日本語/改行/Resize/配置/Clip/DPI別実描画、IME、Popup Focus、
異DPIモニター移動、Multi-Viewport入力/Clip、Vulkan/DX12の描画Target・Close統合・CIの個別記録は別途必要。
今回のFontなし単一Context結果から、これらの成功や性能を推論しない。

## 実行結果

2026-11-16実行。MSVC 14.44.35207、CPU識別子はIntel64 Family 6 Model 165 Stepping 5、論理CPU数20。
[全18ケースのCSV](UI_MODERNIZATION_M0_BASELINE.csv)に時間の平均と最終FrameのCounterを保存。
CppのCount/Bytesのみ120 Frame平均。Cpp Available=0の値は未測定であり、確保ゼロを意味しない。

| 検証 | 結果 |
|---|---|
| Debug UIテストターゲットビルド | 成功・MSBuild終了コード0（下記の環境回避あり） |
| Release UIテストターゲットビルド | 成功・MSBuild終了コード0（下記の環境回避あり） |
| Debug UI回帰全体 + Baseline 9ケース | 成功・終了コード0。最終測定は全ビルド終了後に単独実行 |
| Release UI回帰全体 + Baseline 9ケース | 成功・終了コード0。全ビルド終了後に単独実行 |
| OpenGL実GPU統合 | Debug GPUテストを現ソースでビルドし終了コード0。Arial Font Atlas生成、Texture転送、共有ContextのUI描画・Resize・切り離し/復帰3回を確認 |
| 通常Editor起動・Baseline 9ケースのGPU時間 | 未実施。上記は小規模な既存GPU統合ケースであり、BaselineのGPU計測ではない |
| GPU時間・異DPI・複数Window性能 | 未測定。単一Context/FontなしのBaselineのみ |
| C++ Allocation Profile構成 | 未実施。既存の確保計測回帰は無効構成の契約を確認 |

| 構成 / 10,000 Widget | Build平均ms | Layout平均ms | DrawList平均ms | Geometry平均ms |
|---|---:|---:|---:|---:|
| Debug / static | 19.5836 | 0.0001 | 19.4707 | 15.3274 |
| Debug / update | 25.2458 | 6.0471 | 19.0671 | 15.3496 |
| Debug / scroll | 19.3605 | 0.0001 | 19.2529 | 15.2029 |
| Release / static | 0.6498 | 0.0000 | 0.6258 | 0.6335 |
| Release / update | 0.9642 | 0.3585 | 0.5824 | 0.6190 |
| Release / scroll | 0.6162 | 0.0000 | 0.5934 | 0.6236 |

静的ケースでも10,002 Command/40,008 Vertex/60,012 Indexを生成する。
Clip領域が小さくてもこの経路では保持Widget全体を展開しており、将来のDrawList CacheやVirtualization比較の基準になる。
時間は単一ホストの一回の実行であり、性能保証や厳密な統計的比較ではない。

### 環境依存のビルド条件

初回ビルドは同梱GLFWに`glfw3native.h`がなくC1083で失敗した。
既存のローカル検証資材`artifacts/sg3-deps/GLFW/glfw3native.h`（GLFW 3.4）をCL追加includeとして使用した。
DebugのPDBリンクはMSPDB140.DLL不整合のLNK1101で失敗したため、
最終的に両構成とも既存プロパティ`RavenGenerateLinkDebugInformation=false`で実行した。
リポジトリの依存ファイル/プロジェクト設定は変更していない。

```powershell
$env:CL = '/ID:\Engine\artifacts\sg3-deps'
msbuild Root/Root.sln /m /p:Configuration=Debug /p:Platform=x64 /p:RavenUITest=true /p:RavenGenerateLinkDebugInformation=false
# Releaseも同じ追加条件を使用
```

上記追加includeはこのホスト固有。別環境では同梱GLFW 3.4に合致する正式なNative Headerを用意する。
既存UITextReflowTests.cppの907/908/910/939/940行付近にC4244、DebugリンクにLNK4098を確認。
今回の追加コードに対するコンパイラWarningはなかった。Warning除去はM0の変更に混ぜていない。
ローカルの詳細ログは`ui-m0-debug-*.log`、`ui-m0-release-*.log`（Git対象外）。

### 責務と次の判断

UIElementはLayout区間を返し、UIContextはFrame単位の計測値を所有する。
テストがWidget人口と更新条件を所有し、UITessellatorでGeometryを実変換する。
ベンチマーク固有の設定をRuntimeへ持ち込んでいない。
Raven配下のファイル構成は変更していないためstructure.txtとVisual Studio Filterの追加更新は不要。
限定的なOpenGL実描画確認まで補完した。異DPI/実Editor操作は継続課題。M1は別承認を得て開始した。

### 追加のOpenGL検証手順

```powershell
$env:CL = '/ID:\Engine\artifacts\sg3-deps'
msbuild Root/Root.sln /m /p:Configuration=Debug /p:Platform=x64 /p:RavenUIGPUTest=true /p:RavenGenerateLinkDebugInformation=false
$env:RAVEN_UI_TEST_FONT = 'C:/Windows/Fonts/arial.ttf'
# Root/Rootを作業ディレクトリとして起動。小さい実Windowを表示する。
& D:/Engine/Root/x64/Debug/RavenUIGPUTests.exe
```

ビルド/テストとも終了コード0。既存LNK4098あり。
`ui-m0-gpu-build.log` / `ui-m0-gpu-tests.log`へローカルログを保存。
