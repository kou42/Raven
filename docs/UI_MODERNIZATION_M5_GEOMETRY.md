# M5: Geometry Builder / RHI UI Backend

## 変更と責務

既存UITessellatorのTriangle生成を維持し、UIGeometryBuilderでFramebuffer Pixel Clipと連続Batchを確定する。OpenGLUIRendererとExplicitUIRendererは同じUIRenderGeometryを消費し、元のUIDrawListとの二重走査/Command数依存をなくした。別のTessellatorやRHI体系は追加していない。

```text
Widget/UIContext → UIDrawList（論理座標、TextureView）
  → UITessellator（既存の変形済みVertex / uint32 Index）
  → UIGeometryBuilder（Pixel Clip / 隣接Batch）
  → OpenGL: Buffer upload → Legacy RHI CommandList → state復元
  → Explicit: Acquire前Prepare → Frame内RHISceneCommandList → FenceによるResource保持
```

UIRenderGeometryはVertex/Index/BatchとSourceCommandCount/DrawableCommandCountを持つ。UIRenderBatchは連続FirstIndex/IndexCount、左上原点Pixel Clip、TextureView/UseTextureを保持する。TextureViewのRefはCPU準備中のAsset/Texture寿命を維持し、実GPU参照は既存BackendのFrame/Fence保持に委譲する。新しい非所有Texture pointerやGPU Resource registryは追加しない。

結合条件は連続Index、同じPixel Clip、同じTexture source種別と参照先、同じTexture使用状態。Commandを並べ替えず、透明な重なりのPainter順序を維持する。色とUVは各Vertexに残るため、同一TextureのGlyphや異なるUV原点のImageも結合できる。完全Clip外/ゼロ面積/Indexなしは描画しない。描画されないCommandを挟む非連続Index範囲は結合しない。

## 座標・RHI契約

DrawListの座標はUIContextの論理Viewport単位。Content ScaleによるFont/Style調整とは独立に、Framebuffer / logicalSizeでPixel倍率を決める。Clip端は論理ViewportへClampし、左上をfloor、右下をceilで外側へ丸め、実Framebuffer端へ再Clampする。ゼロ幅の論理Clipを丸めによって1 Pixelへ拡大しない。計算はdoubleで行い、無効な寸法・描画対象の非有限Clip・Legacy Buffer上限超過を拒否する。

共通Clipは常に左上原点。OpenGLだけがY=FramebufferHeight-(Y+Height)で左下原点へ変換し、既存のRenderTarget/Viewport/Scissor/Pipeline/Texture binding/state復元を維持する。DX12/Vulkanは左上原点Scissorをそのまま使用する。firstIndexは両CommandListともuint32 Index要素単位で、byte offsetやbaseVertexではない。

Explicit Prepareに論理サイズを受け取るoverloadを追加。既存overloadはFramebufferと1:1の入力として委譲する。IExplicitSceneRuntime::SubmitUIFrameは論理サイズを省略可能な引数として追加し、ApplicationがUIContext.GetViewportSize()を渡す。補助Windowも自身のWindow論理サイズとFramebufferを使う。ゼロの省略値だけを1:1へ解釈し、部分的/無効な寸法はBuilderで失敗させる。既存3引数のソース呼出は維持するが、virtual signature/PreparedExplicitUIの構造は変わるため外部バイナリ利用者は再コンパイルが必要。

RHI機能の監査結果:

| 必要機能 | 対応 |
| --- | --- |
| RenderTarget | Legacy RHIの退避/復元とExplicit Frameの既存off-screen Begin/Endを再利用 |
| Scissor / Viewport | 両CommandListに存在。単位・原点を明文化し、共通BuilderのPixel値を使用 |
| Draw offset | 既存firstIndexをBatch範囲へ適用。Vertexは共有しIndex順序を維持 |
| Buffer / Texture | 既存RHIDevice生成、Acquire前Prepare、Frameによる参照保持を維持 |
| UI Frame境界 | 通常UIRenderer::RenderへExplicit Acquireを混在させず、既存Explicit RuntimeのPrepare/Drawを使用 |
| Alpha合成 | RHIAlphaBlendModeを追加。Sceneの既定SourceOverを維持し、Explicit UIのみOpenGL互換を選択 |

実画像比較で、従来OpenGL UIはAlpha出力が As*As + Ad*(1-As)、Explicitは As + Ad*(1-As) だったことを確認した。RGBの式は同じだがRGBA比較では差になる。共通Pipeline設定のSourceAlphaWeightedでExplicit UIをOpenGLへ合わせた。Sceneの既定SourceOverとOpenGL出力は変更していない。Vulkanの既存半透明Pixel期待値もUIのAlpha契約に合わせて255から191へ更新した。

## 検証結果

2026-11-18、Windows x64、MSVC 14.44.35207、NVIDIA GeForce RTX 3080。

| 確認 | 結果 |
| --- | --- |
| Debug/Release UIビルド・全CPU回帰 | 成功、各終了コード0 |
| CPU Geometry回帰 | Vertex/色/UV/Index順序維持、隣接Texture/Clip分割、Index offset、上下UV原点、非整数倍率、Window間倍率分離、空Clip、寸法不正、空Frameが成功 |
| OpenGL既存GPU回帰 | Font/Texture、Cache off/on、共有Contextの切り離し/復帰各3周期が成功 |
| OpenGL / Vulkan / DX12画像比較 | 実Font AtlasのText、既存SVG、半透明の重なり、Clipを含む同じDrawListを実GPUで比較。下記条件で成功 |
| Vulkan色出力回帰 | UNORM/sRGB mutable output、Clear/opaque/blend/Viewport Readback、Resizeが成功 |
| DX12 / Vulkan補助Window | Device/Viewport共有、Resize、Close各3周期が成功 |
| 差分/構成 | git diff --check成功。新規Renderingファイル2つをstructure.txtとvcxproj.filtersへ登録 |

GPU比較はOpenGLのBack Buffer、VulkanのSwapChain、DX12のRGBA off-screen Targetを読み戻し、行方向/BGRAを正規化して比較する。DX12 Readbackは元のSampling stateへ戻し、既存ResizeのFence待機後に読む。FontはArial 18pxの同一CPU Atlas Asset、SVGはAssets/Svg/AnimatedRect.svgの初期状態である。

| Framebuffer / 論理倍率 | Vulkan vs OpenGL | DX12 vs OpenGL |
| --- | --- | --- |
| 320×240 / 1.0 | 全RGBA完全一致（最大差0） | 全RGBA完全一致（最大差0） |
| 400×300 / 1.25 | 辺上111 Pixelを除きRGBA差3/255以内 | 辺上111 Pixelを除きRGBA差3/255以内 |
| 320×240 / 2.0 | 全RGBA完全一致（最大差0） | 全RGBA完全一致（最大差0） |

1.25倍ではPixel中心が三角形の辺上へ重なる箇所にAPI間の充填/微小丸め差がある（最大差127）。テストは任意の「周囲1 Pixel」を除外せず、実際に描画するBatchのClip内で、共有Geometryの辺とPixel中心との距離0.0001 Pixel以内の点だけを区別する。それ以外のPixelに3/255を超えるRGBA差があれば失敗する。非整数倍率を全画素完全一致したものとは扱わない。

## Batch計測

[測定CSV](UI_MODERNIZATION_M5_MEASUREMENTS.csv)にDebug/Release計24ケースを保存した。100 / 1,000 / 10,000 Rect Command、同一Clipと交互Clip、結合off/on。論理1280×720、Framebuffer1600×900、Warmup 5回 + 測定30回の平均。GPUなしでTessellationからBatch生成までを測り、ビルド/他テストとの同時実行を避けた。

| 10,000 Command | off Batch数 | on Batch数 | Debug ms off→on | Release ms off→on |
| --- | ---: | ---: | ---: | ---: |
| 同じClip | 10,000 | 1 | 18.988 → 17.121 | 1.161 → 0.920 |
| 交互Clip | 10,000 | 10,000 | 20.540 → 20.678 | 1.207 → 1.220 |

各ケースのVertex 40,000 / Index 60,000は不変。同一ClipではBatch作成を省略できる一方、交互Clipでは結合判定の小さい追加コストが残る。既存Tessellator単体との速度差、実GPU時間、Editor全体のFrame改善率は測っていない。Geometry/Upload量は減らない。既存UIFrameMetrics.DrawCommandCountはDrawListの論理Command数のままであり、GPU Batch数へ読み替えない。

## 再実行

```powershell
# Repo Root。依存は既存bootstrapで準備する。追加GLFW includeは不要。
msbuild Root/Root.sln /m /p:Configuration=Debug /p:Platform=x64 /p:RavenUITest=true /p:RavenGenerateLinkDebugInformation=false
# ReleaseはConfiguration/Pathを変更する。
Set-Location Root/Root
& D:/Engine/Root/x64/Debug/RavenUITests.exe
& D:/Engine/Root/x64/Debug/RavenUITests.exe --measure-ui-geometry
```

GPUはRavenUITestの代わりにRavenUIGPUTest=trueでビルドし、Root/RootからRavenUIGPUTests.exeを起動する。RAVEN_UI_TEST_FONT=C:/Windows/Fonts/arial.ttfを指定し、引数なし、--ui-geometry-parity、--vulkan-color、--vulkan-auxiliary、--dx12-auxiliaryをそれぞれ実行する。GPUテストは対話Desktopと対応Driverが必要であり、通常CIではCPU回帰とGPU exeのビルドを確認する。

ホスト固有のPDB回避としてRavenGenerateLinkDebugInformation=falseを継続使用した。既存CPUテスト由来C4244とDebug LNK4098は継続。新規箇所のコンパイラWarningは確認されなかった。ログはui-m5-*-build.log / ui-m5-*-tests.log / ui-m5-gpu-*.log / ui-m5-geometry-*.log。

## 残課題

物理Monitor間のDPI変更、全Font/日本語IME/全SVG仕様、全GPU Driverは未検証。補助Windowの回帰は描画成功/共有/Resize/Closeを確認しており、全補助Window画像のPixel一致までは確認していない。Batchは隣接互換範囲だけで、Texture atlasの再配置や描画順を変える最適化は行わない。Device/Windowごとの既存Texture cacheとPrepare/Frame Lifetimeを維持し、永続GPU Geometry cacheは追加しない。

新規RuntimeファイルはUIGeometryBuilder.h/.cppのみ。次はM6のHeadless UI Testing（コマンド正規化/Snapshot）であり、M5のCPU Geometry回帰をM6全体の完了とは扱わない。M6は別承認とする。
