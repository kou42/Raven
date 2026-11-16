# UI Modernization M2: Subtree DrawList Cache

## 構造と適用範囲

UIElementに明示opt-inのSetDrawListCacheEnabledを追加した。
有効化したElementだけがSnapshot用Stateを所有し、子孫にはStateを自動作成しない。
既定は無効で、既存Editor/Runtimeの全Widgetへ一括適用しない。
UIContext→保持Tree→UIDrawList→既存Tessellator/Rendererの責務境界を維持する。

型として対応するのは、正確にUIElementであるContainerと、finalなUIPanel/UICircle/UIPolygon。
SupportsDrawListCacheの基底実装はtypeidの完全一致を使い、独自派生型のOnBuildを暗黙に省略しない。
能力判定は静的な型契約であり、overrideする型は全表示更新・可変外部データの無効化を監査する必要がある。
型監査結果はSubtreeごとに保持し、追加/削除/移譲等のTree変更時に祖先分も再監査する。
非表示の未対応Widgetも判定対象とする。

UILabel/UIImage/UIButton/Scroll/Tree/Table/Tab/Window/Tooltip/UISvgなど未対応の型を含むSubtreeは通常生成に戻る。
対応外の親の下でも、独立した対応Subtreeを明示有効化できる。
子のCache無効は「その位置にSnapshotを保存しない」という意味であり、対応型の子を祖先Snapshotから除外する指定ではない。

```cpp
auto staticGroup = CreateScope<UIElement>();
// 静的なUIPanel等をstaticGroupへ追加する。
staticGroup->SetDrawListCacheEnabled(true);
context.AddRootChild(std::move(staticGroup));
```

## Snapshotと無効化

Snapshotは最終Layout座標・World Transform・Screen ClipのCommand列を保持する。
局所座標への逆変換や、Clip済みCommandの再baseは行わない。
再利用条件は、型監査成功、Snapshot有効、Visual Dirtyなし、絶対位置/Size/World Transform/継承Clip/継承Tint・Opacityの一致。
浮動小数点は完全一致で比較し、小さい変化を見逃すepsilon判定は導入しない。

M1のPaint/Geometry/Style、Measure/Arrange、Theme/DPI、Context変更からSnapshotを失効させる。
Layout親の変更で移動した子は、ArrangeRecursiveで実位置/実Sizeの変化を検出して失効させる。
同値Arrangeは失効させず、動的Subtreeと並ぶ静的Subtreeの再利用を維持する。
親Transform/Opacity/子Clipの変更はM1の子孫伝播と境界条件比較の両方で保護する。

Dirtyを伴って毎Frame更新されるSubtreeは通常生成し、Snapshot用コピーを作らない。
変更のないFrameを通常生成した際にSnapshotを採取し、その次のFrameからHitする。
初回・変更Frameの生成結果を同時に保持用コピーする方式と比べ、動的UIの追加負荷を抑える。
親条件だけが変わる場合も、古いSnapshotは使わず当Frameの通常生成結果へ戻る。
OnBuild中に再無効化された場合はSnapshotを確定しない。

UIDrawList::AppendRangeがCommandを値コピーし、Polygon頂点配列の所有権も分離する。
Hit時にも今Frameの最終UIDrawListを構築するため、GPU描画やGeometry変換を省略する機能ではない。
Painter順の走査位置でそのままAppendし、Sibling順変更後も描画順を維持する。
Popup/TooltipはContextの専用Layer、Drag Preview/Frame Overlayは従来の後段生成を継続する。
通常SubtreeのSnapshotへFrame Overlayを含めない。

## Resource・Text・SVGの境界

UIDrawListの一般的なRangeコピーはTexture Refを保持し、コピー先ClearまでResourceを生存させる。
一方M2のSnapshot採取はImage/TextureAsset/UITextureViewを持つCommandを拒否する。
監査能力を誤ってoverrideしたWidgetも、このResource検査で保守的に通常生成へ戻る。
同一Texture/Font参照でも、内容や寸法が変化し得るため、Refの生存だけでは再利用を保証できない。

TextはGlyph Quad生成・Font Atlasの内容世代/配置変更を扱うまで対象外。
既存Font Atlas/DPI Cacheは変更しない。SVG固有のimport/animation/tessellationも対象外。
静的なUIPolygonへ既に変換された頂点列のみ対応し、SetPointsで失効する。
Polygon三角形化などUITessellatorの処理はCache Hitでも毎Frame実行する。

StateはElement所有のScopeで保持し、親/Contextへの非所有pointerを保存しない。
Disable/Element破棄時にStateとSnapshotを解放する。失効後の通常生成時に古いCommandをClearし、capacityを再利用する。
非表示のSnapshot容量は再表示/Disable/破棄まで残り得るが、非表示時にはReplayしない。

## 診断値

UIFrameMetricsへDrawListCacheのHit/Miss/Capture/Rejected/ReplayedCommandCountと、保持Bytes/Command数を追加。
Hitは再利用したSubtree数、Captureは新規Snapshot採取数、Missは対応Subtreeで通常生成へ戻った回数。
Rejectedは型監査または採取時のResource検査で拒否した回数。Dirtyで採取自体を行わないFrameはResource検査も省略する。
保持Bytes/Command数は、祖先Hitで子の描画走査を省いても、既存ElementCount走査で全Stateを集計する。
保持Command数は容量中の保存データ数であり、そのFrameの有効Snapshot数を意味しない。
BytesはState本体+Command/Polygon vector容量で、Allocator管理領域やUIElement内のScope slotは含まない。
Scope slotは無効時も各UIElementに存在する。既存Statistics Panel/CSVの列は維持する。

## 検証と測定条件

- CPU: Cache有無で型/矩形/Color/UV/Polygon/Transform/Clip/Resource参照を比較。
- 親Transform/Opacity/Clip、Size/Position、Theme/DPI、Painter順、Layout親からの移動、非表示/復帰、Context移譲。
- Popup/Tooltip/Frame Overlayと共存させ、Overlay更新がSnapshotへ固定されないことを確認。
- 未対応派生型の通知なし更新、子の追加/削除による再監査、ネストした保持容量、Disable時の解放。
- RangeコピーのTexture寿命と、Resource CommandのSnapshot拒否/破棄後の解放。
- GPU: 既存OpenGL統合をCache off/onで実行し、各描画を3 Frame継続してHitを確認してからPixel readback。

Benchmarkは100/1,000/10,000 Panel、static/dynamic/mixed90_10、Cache off/onの18ケース。
混在は90%静的Groupと10%動的Groupに分け、動的Groupは毎Frame全Panelの幅95/96pxと色を交互更新する。
単一Context、1280×720、DPI=1、Font/Texture/Popupなし。30 Warmup後120 Frame平均。
SettersはBuild/EndFrame計測の外側。EndFrame全体時間には描画量/Element数/Cache容量の集計も含める。
Build/EndFrame/Layout/DrawListのCPU時間、Counter、最終保持メモリとCommand/Vertex/Indexを記録する。
時間の固定閾値は回帰条件にせず、静的GroupだけHitし、動的Groupを再コピーしないことをCounterで検証する。

実行手順は既存RavenUITest=true。ローカルのGLFW追加includeとPDB回避はM0資料を参照。
GPUはRavenUIGPUTest=trueでビルドし、Root/Rootを作業Directory、RAVEN_UI_TEST_FONTに実在TTFを指定する。

## 実行結果

2026-11-17、基準dc5b1a5d（M0/M1マージ済み）にM2差分を適用して実行。
Windows x64 / MSVC 14.44.35207、Intel64 Family 6 Model 165 Stepping 5、論理CPU数20。
[全36ケースの測定CSV](UI_MODERNIZATION_M2_MEASUREMENTS.csv)を保存。
全ビルド終了後にRelease→Debugを逐次実行し、測定中は他のビルド/GPUテストを同時実行していない。

| 確認 | 結果 |
|---|---|
| Debug/Release UIテストターゲットビルド | 両方成功・終了コード0 |
| Debug/Release UI回帰全体 | 両方成功・終了コード0。M0/M1の既存回帰、新Cache回帰、各18ケースのCache計測を含む |
| OpenGL GPUテストDebugビルド/実行 | 両方成功・終了コード0。Cache off/onの各経路で共有Context描画/Resize/切り離し復帰3回を確認 |
| GPU Cache実使用 | Cache on経路では3 Frame目のHitを検査した上でred pixel readback、GL errorなし、Polygon Mode復元を確認 |
| Font Atlas/Texture転送 | Arialで既存GPU統合を実行し成功。FontそのものをCacheした検証ではない |
| 空白/差分確認 | git diff --check成功 |
| 実Editorの手操作、異DPIモニター、DX12/Vulkan実描画、GPU時間 | 未実施/未測定 |

既存のC4244（UITextReflowTests.cppの数値変換）とDebug UIテストのLNK4098は継続。
今回の追加箇所に対するコンパイラWarningは確認されなかった。
ビルドのGLFW Native Header追加include/PDB回避はM0資料のホスト固有条件を再利用した。
リポジトリの依存ファイル/ビルド設定は変更していない。

### 10,000 Widgetの比較

時間はms、Cache Bytesは最終Frameの保持量。値は一回の実行の平均であり統計的保証ではない。

| 構成 / ケース | off Build | on Build | Build削減率 | off EndFrame | on EndFrame | Cache Bytes |
|---|---:|---:|---:|---:|---:|---:|
| Debug / static | 21.4420 | 3.6561 | 82.9% | 22.1685 | 4.2783 | 2000128 |
| Debug / dynamic | 26.7167 | 26.7686 | -0.2% | 27.3740 | 27.4373 | 128 |
| Debug / mixed90_10 | 25.1484 | 9.2926 | 63.0% | 25.8121 | 9.9179 | 1800256 |
| Release / static | 0.6446 | 0.1624 | 74.8% | 0.7142 | 0.2449 | 1920120 |
| Release / dynamic | 1.0358 | 1.0349 | 0.1% | 1.1035 | 1.1033 | 120 |
| Release / mixed90_10 | 0.9045 | 0.4857 | 46.3% | 0.9689 | 0.5656 | 1728240 |

Release静的ケースはBuild約74.8%、EndFrame全体約65.7%削減。
静的90%/動的10%の混在はBuild約46.3%、EndFrame全体約41.6%削減。
追加のSnapshot保持は静的約1.92MB、混在約1.73MB（十進表記）。
既存の最終DrawList保持量2,330,496 Bytesはoff/onで同じであり、Snapshotメモリはその上に追加される。
Scope slotを含むUIElement全体/Allocator overhead/GPUメモリはこの保持量に含まない。

全動的ケースはHit/Captureとも0、保存Command数0。StateのみRelease 120 Bytes、Debug 128 Bytesを保持する。
小規模の動的ケースでは追加分岐等の負荷があり、全ケースでの性能向上は主張しない。
動的部分は無効のままにし、大きな静的Groupを選んで有効化することを推奨する。
36ケースすべてでCommand/Vertex/Index数はCache off/onで一致した。

### 再実行とログ

```powershell
$env:CL = '/ID:\Engine\artifacts\sg3-deps'
msbuild Root/Root.sln /m /p:Configuration=Release /p:Platform=x64 /p:RavenUITest=true /p:RavenGenerateLinkDebugInformation=false
# Root/Rootを作業Directoryとして実行。Debugは構成/Pathを変更する。
& D:/Engine/Root/x64/Release/RavenUITests.exe *> D:/Engine/ui-m2-release-tests.log
Select-String '^cache_baseline,' D:/Engine/ui-m2-release-tests.log
```

GPUは同じ追加条件でRavenUITestの代わりにRavenUIGPUTest=trueを指定し、
RAVEN_UI_TEST_FONT='C:/Windows/Fonts/arial.ttf'でRavenUIGPUTests.exeを実行する。
ローカルログ: ui-m2-debug-build.log / ui-m2-release-build.log / ui-m2-gpu-build.log / ui-m2-*-tests.log。
既存M0のbaseline CSV行も同じ実行に含むが、M2比較CSVはcache_baseline行だけを保存している。

### 残課題

既存Editorへ自動適用せず、利用側が監査済みの静的Groupを明示指定する段階である。
Text/Texture/SVG/未監査Widgetへの拡張、Resource内容世代、Geometry Cache/Batching、複数Windowの性能比較は含まない。
GPU描画数を減らすものではなく、描画コマンド生成CPUの最適化として扱う。
次はM3のInspector View/Controller依存整理を別承認で進める。
Raven配下のファイル追加/削除/移動はなく、structure.txtとVisual Studio Filterの構成変更は不要。
