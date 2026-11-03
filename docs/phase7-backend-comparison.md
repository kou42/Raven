# Phase 7 Backend比較・長時間操作

## 固定Sceneで起動する

通常GameはAsset読込やDemo Layerを含み、Backendごとに利用可能な機能・Entity数が異なる。
比較には次の専用起動モードを使う。複数引数は順序を問わず処理され、未知の引数は拒否される。

```powershell
Set-Location D:/Engine/Root/Root
../x64/Release/Root.exe --backend=opengl --editor-backend-comparison
../x64/Release/Root.exe --backend=dx12 --editor-backend-comparison
../x64/Release/Root.exe --backend=vulkan --editor-backend-comparison
```

上記は一つずつ起動・終了する。Raven UI専用で、ImGui/DualやUI Migration Demoとは併用しない。
比較用Sceneは通常のApplication/Editor/Viewport/Multi-Viewport経路へ接続する。
通常起動のGame/Title・Character/Cloth/Jelly/Fluid・Debug専用UI Demoは追加しない。
Engineを変更して比較する場合は、同じ実行ファイルの3 Backendを使う。

`static-cubes-v1` の条件:

- Camera 1、Cube 3、Floor 1の計5 Entity。描画Meshは4。Cube 3個は同じMeshを共有する。
- CPU Geometryは既存Primitive Factoryから生成。Cubeは24頂点/36 Index、Floorは4頂点/6 Index。
- +Y up、単位m、Camera forwardは-Z、回転はradian。Cubeは2m角で床上面Y=0に接する。
- Game CameraはPosition `(0,8,18)`、Pitch `-0.28`、FOV 45度、Near 0.1、Far 1000。
  Projectionは既存SceneCameraSystemで計算し、Window Resizeを反映する。
- 頂点色×Tint `(1,1,1,1)`、Opaque、Cullなし、Depth LessEqual、Depth Writeあり。
  Raster/Depth/Blendは比較用Sceneの共通Pipeline ProfileをOpenGL/Explicitの両方で利用する。
  OpenGL UniformとRHI Tintをそれぞれ明示。Explicitは既存の白Texture fallbackを使用する。
- Simulationを停止し、時間経過でTransformや描画量を変えない。Editorの編集は既存Command経由。
- Mesh/MaterialはScene Entityが共有所有し、Scene終了時の既存Cleanupで解放する。

起動ログの `[UI comparison]` 行には実際のEntity名・Transform・Geometry数・Tint・Camera設定を出力する。
3 Backendのログを照合し、Entity 5 / Mesh 4だけでなく内容の一致を確認する。
編集後の状態はこの起動Manifestには反映されないため、測定前に値を戻すか再起動する。
Geometryの同一性は共通FactoryとCPU回帰で保証し、Manifestの頂点数だけで別Geometryを同一と判定しない。
Pixel完全一致、GPU時間、Asset/Physics/Animationの負荷比較はこの固定Sceneの合格に含めない。

2026-11-03にVulkanのUNORM出力契約を統一し、固定Sceneの明るさの差を修正した。
GPU参照色・mutable SRGB fallback・Resizeと、3 Backend Main/Vulkan補助Windowの表示を確認済み。
詳細は `RAVEN_IMGUI_PHASE7_VALIDATION.md` を参照。DPI別検証・正式性能Baselineは未完了で、参考CSVから性能順位を付けない。
またStatisticsのBackend別診断内容により、同じWidget数でもDraw Command数は一致しない。
測定時はPanelの選択・Scroll位置・診断設定を記録し、表示量の差も併記する。

## 条件

- 通常Release x64、Raven UI、100% DPI。同じScene、選択Entity、Panel、Window寸法、Profiler設定を使う。
- OpenGL / DirectX 12 / Vulkanを順に実行し、性能測定中は他Backendやビルドを並走させない。
- MainのみとScene/Game/Inspectorを分離した補助3 Windowを比較する。配置JSONと選択Tabを保存する。
- 起動・Shader/Pipeline準備後にウォームアップし、静止状態を60秒記録する。
- 各Backendで30分操作。5分ごとにTransform編集→確定→Undo→Redo→Undo、Tab分離/Close復帰、Resizeを行う。
- 操作中と静止時を混ぜてBackend順位を決めない。長時間後は同じ配置・選択・寸法に戻して60秒記録する。
- Main最小化中は補助更新も止まる既存仕様。停止中のCPU時間を性能改善と解釈しない。

## 安全な採取

開始前に `RavenEditorDock.layout.json` と `.bak` を退避し、終了後に元ファイルを復元してSHA-256を照合する。
既存のCSVは上書きしない。Editorの終了コードだけで合格にせず、WindowCloseと描画エラーも確認する。

StatisticsのStart / Restart → Stop → Save CSVを使う。非表示でも記録は継続する。
CSVは時間系列ではなくセッションの平均・最大値であり、p95/p99やGPU時間は求められない。
開始・終了を独立したセッションとして保存する。通常ビルドのC++ AllocationはN/A。
DrawList Allocationは別の計測値であり、プロセス全体の確保回数には代用できない。

```powershell
./scripts/watch-ui-process.ps1 -TargetProcessId 1234 `
    -OutputPath artifacts/phase7/backend-comparison/opengl-memory.csv `
    -DurationSeconds 1800 -IntervalSeconds 10
```

PIDは実際のEditorのものを渡す。監視はアプリへ入力せず、終了させない。
UTC時刻、経過秒、Working Set、Private Bytes、Handle、Thread、累積CPU秒を保存する。
Working SetはOSのページ管理にも依存するため、増加だけでリークと断定しない。
Private Bytes・Handle・Widget保持量の推移と、同じ操作を繰り返した後の状態を合わせて判断する。

## 集計

manifest CSVの例（csvはmanifestからの相対パス、または絶対パス）:

```csv
backend,configuration,dpi,scenario,stage,csv
opengl,Release,100,main,start,opengl-main-start.csv
opengl,Release,100,main,end,opengl-main-end.csv
dx12,Release,100,main,start,dx12-main-start.csv
```

```powershell
./scripts/compare-ui-recordings.ps1 `
    -ManifestPath artifacts/phase7/backend-comparison/manifest.csv `
    -OutputPath artifacts/phase7/backend-comparison/summary.csv
```

全Window集計の12指標をBackend・条件・区間付きのCSVへ展開する。
旧Mainのみ形式は拒否し、未計測を0へ変換しない。入力記録のWindow別行は原本へ残る。
Window数、Sample数、採取時間、描画内容が一致することを原本・操作記録で確認してから比較する。
異なる条件の入力を指定すること自体は禁止しないため、summaryの存在だけでは比較成立の証拠にならない。

## 合否と残課題

表示は文字、Texture上下、Aspect、Clip、補助Window本文を確認する。
操作は編集結果、履歴、Focus、Close復帰、Resize後の全Window生存を確認する。
性能はUI CPU平均/最大、Draw Command、Vertex/Index、Texture切替、保持量を記録する。
性能予算が未確定のため、絶対値による合否やBackend優劣を先に決めず、条件と観測値を報告する。
30分起動したままにするだけでは「30分操作」の合格にしない。
高DPI、異DPI、Debug実操作、全Component/IME、Scene交換は別途残課題として扱う。
