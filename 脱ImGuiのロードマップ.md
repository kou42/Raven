# Raven 脱ImGuiロードマップ

最終更新: 2026-10-09

対象: Raven Editor / Debug UI / Dear ImGui統合

関連文書: [`RAVEN_UI_ROADMAP.md`](RAVEN_UI_ROADMAP.md)、[`RAVEN_UI_PHASE2_TEST_AUDIT.md`](RAVEN_UI_PHASE2_TEST_AUDIT.md)、[`RAVEN_UI_PHASE4_TEST_AUDIT.md`](RAVEN_UI_PHASE4_TEST_AUDIT.md)

## 1. 目的

Dear ImGuiが担っているEditorとDebug UIをRaven UIへ段階的に移行し、最終的に次の依存を削除する。

- `Raven/ImGui/ImGuiLayer.*`
- `Layer::OnImGuiRender()`
- Raven側コードからの`imgui.h`および`ImGui::*`参照
- Visual StudioプロジェクトからのDear ImGui core / GLFW backend / OpenGL3 backend
- `vendor/imgui` submoduleと`imgui.ini`

脱ImGuiは、単にWidget呼び出しを置換する作業ではない。現在Dear ImGuiが暗黙に提供しているFrame管理、入力Capture、Window、Docking、Panel状態、Clip、Texture表示、Popup、Keyboard NavigationをRaven側の明示的な責務へ移す作業として扱う。

本書はRaven UI自体の機能追加順を扱う`RAVEN_UI_ROADMAP.md`とは役割を分け、既存Editorをどの順序で移行し、どの条件でDear ImGuiを削除できるかを定義する。

## 2. 完了の定義

次のすべてを満たした時点を「脱ImGui完了」とする。

- OpenGL / DirectX 12 / Vulkanの対応対象BackendでEditorとDebug UIがRaven UIだけで動作する。
- Scene View、Game View、Entity Picking、Gizmo、Hierarchy、Inspector、Statistics、Animation Debug、各Debug OverlayがRaven UIで利用できる。
- Dock layoutの保存・復元、Tab移動、補助OS Windowへの分離・復帰がRaven UI経路で動作する。
- Mouse / Keyboard / Clipboard / IME / DPI / Focus / Captureの回帰テストを通過する。
- `rg "ImGui|imgui"`のRaven製品コード・project設定・通常ビルド対象での結果が0件になる。移行記録内の文言は除外してよい。
- Dear ImGuiを外した状態で通常ビルド、Raven UI CPUテスト、GPU統合テスト、主要Editor操作のSmoke Testが成功する。
- Dear ImGui無効時専用のfallbackや二重入力経路が残っていない。

## 3. 現在の構造

### 3.1 すでに利用できるRaven UI基盤

2026-10-09時点のコードでは、脱ImGuiに必要な土台の多くがすでに存在する。

- `UIContext`がRetained Tree、Layout、Hit Test、Mouse Capture、Keyboard Focus、IME、Popup、Drag & Drop、Frame DrawListを管理する。
- `UIDrawList`と`UIRenderer`がCPU側UI表現とGPU Backendを分離する。
- `OpenGLUIRenderer`と`ExplicitUIRenderer`により、OpenGL / DirectX 12 / VulkanのUI描画経路がある。
- Button、Checkbox、Slider、InputText、InputNumber、ComboBox、TreeView、Table、Tab、Window、Scroll、Tooltip等のWidgetがある。
- `UIDockSpace`がSplit、Tab移動、Dock Preview、Layout保存・復元を扱う。
- Main Windowと補助Windowごとに`UIContext`を持つMulti-Viewport経路がある。
- `UIImmediateContext`が安定IDを使ってRetained Widgetを再利用し、Debug UI向けのImmediate風APIを提供する。
- `RavenUITest`と`RavenUIGPUTest`の独立した検証入口がある。

したがって、今後の中心課題は新しいUI基盤をもう一つ作ることではなく、Editor固有責務をRaven UIへ接続し、並行経路を安全に収束させることである。

### 3.2 現在のDear ImGui依存

コード検索上、Dear ImGuiのLifecycleまたはAPIへ触れるRaven側ファイルは17ファイル、`ImGui::*`呼び出しは約529箇所ある。数値はこの文書作成時点の目安であり、進捗判定では毎回再集計する。

| 領域 | 現在の責務 | 主な移行上の注意 |
| --- | --- | --- |
| `Application` / `ImGuiLayer` | Context生成、GLFW/OpenGL backend、Frame Begin/End | Raven UI Frameと二重管理。Explicit BackendではImGuiを生成しない |
| `EditorLayer` | DockSpace、Menu、Scene/Game View、Panel表示制御 | 移行の中心。Framebuffer表示と入力座標変換が高リスク |
| Scene Hierarchy | Entity一覧、展開、選択、名称変更 | Scene寿命、Entity generation、Command Historyとの境界が重要 |
| Inspector | Component編集 | 編集中値、確定条件、Undo/Redo、破棄済みEntity参照に注意 |
| Statistics | Profiler、Renderer、Physics統計 | 表示量が多い。Snapshot化しやすく先行移行に向く |
| Animation Debug | Animator状態・操作 | Runtime参照をPanelへ所有させない |
| Gizmo | 描画、Hit Test、Drag操作 | Viewport座標、行列積順、Mouse Capture、Undo確定が高リスク |
| Character / Fluid Debug Overlay | Runtime調整・診断 | Debug専用。`UIImmediateContext`へ移しやすい |

### 3.3 現在のFrameと入力の流れ

現在は概ね次の順序で動作する。

```text
Window Event
  -> Application
  -> Raven UIContext
  -> 未処理ならLayerを後積み優先で伝播

Application Frame
  -> Raven UI Immediate宣言
  -> Raven UIContext::BeginFrame
  -> Scene Update / Render
  -> Layer Update / Render
  -> ImGuiLayer::Begin
  -> Layer::OnImGuiRender
  -> ImGuiLayer::End
  -> Raven UIContext::EndFrame
  -> Present
```

Raven UIは現在ImGuiより後にOverlayとして描画される。移行期間に両方のUIを同じ位置へ表示すると、描画順だけでなく入力所有権も曖昧になる。各機能は「ImGui版またはRaven版のどちらか一方だけを有効にする」切替単位を持ち、同一領域で両方を操作可能にしない。

## 4. 目標アーキテクチャ

```text
Application / WindowManager
  |- Window Event、Frame、DPI、IME、Clipboard
  |- WindowごとのUIContext / UIRenderer
  `- Editorの具体的なPanelを知らない

EditorLayer
  |- Editor selection / Camera / Command History
  |- Editor UI Root / DockSpace / Workspace状態
  |- Scene View / Game ViewのRenderTarget接続
  `- Panel Presenterへ必要な非所有参照を渡す

Editor Panel
  |- Model / Snapshot: Runtimeから読む表示用データ
  |- Action / Command: Runtimeへ返す操作
  `- Raven UI View: Widget生成・更新だけを担当

Raven UI
  |- UIContext: Lifetime、Layout、Focus、Input
  |- UIDrawList: API非依存描画要求
  `- UIRenderer: Backend固有描画
```

重要な境界は次のとおりとする。

- `Application`へEditor固有WidgetやPanel分岐を追加しない。
- PanelはScene、Entity、Animator、PhysicsWorld等を所有しない。FrameごとのSnapshotまたは検証可能な非所有参照を受け取る。
- 編集操作は既存の`EditorCommandHistory`とCommand型を通す。UI callbackからRuntime値を無制御に直接変更しない。
- Scene View / Game ViewはGPU API固有Texture IDをWidgetへ渡さない。Raven UIのImageがEngine側TextureまたはRenderTarget Viewを参照し、`UIRenderer`がBackend Resourceへ解決する。
- Gizmoの計算・Hit Test・Drag stateはUI描画APIから分離し、Raven UIは入力領域と描画結果の提示だけを担当する。
- Runtime向けUIとEditor向けUIは、同じ基盤を利用してもRoot、Render Target、入力対象を明示的に分ける。

## 5. 移行原則

1. 小さいDebug UIから移し、最後にViewportとEditor shellを移す。
2. Panelごとに表示データと操作をImGui呼び出しから先に分離する。
3. 1つの機能に2つの書き込み経路を常設しない。比較期間中も操作可能なのは片方だけとする。
4. 既存Safety Check、Scene寿命検証、Entity generation検証、Undo/Redoを維持する。
5. OpenGLだけで完了判定しない。Raven UIの利点であるRHI非依存性を維持する。
6. 移行のためだけにImGui互換APIをRaven UI上へ丸ごと再現しない。Debug UIはImmediate風API、EditorはRetained WidgetとPresenterを使い分ける。
7. 各Phaseは「コードがある」ではなく、テストと実操作のExit Gateを満たして完了とする。

## 6. 実装フェーズ

### Phase 0: Baseline固定と移行スイッチ

目的: 移行中の比較、切り戻し、依存数の計測を可能にする。

- [x] ImGui依存ファイル数、API呼び出し数、project参照、実行時生成条件を`scripts/measure-imgui-dependencies.ps1`で再計測できるようにする。
- [x] Editor UI Backendを`DearImGui` / `RavenUI` / 比較用`Dual`から選べる`EditorUIBackend`へ集約する。
- [x] `Dual`では`EditorLayer::PanelUIOwnership`によりPanel単位の所有Backendを明示し、同一Panelを両方から編集可能にしない。
- [x] Scene選択、Component編集、Dock layout、Viewport操作の現行Smoke Test手順を`docs/imgui-migration-test-scene.md`へ記録する（2026-10-10時点の実環境実施結果は未取得）。
- [x] CPU/GPU UIテストと通常DebugビルドのBaseline結果を`docs/imgui-migration-test-scene.md`へ保存する（通常Debug成功、CPU実行失敗、GPUコンパイル失敗。失敗内容は継続課題として明記）。

Exit Gate:

- 同じ実行ファイルでPanel単位の移行状態を識別できる。
- 切替設定がRuntime SceneやAsset固有設定へ混入していない。
- ImGui無効であるExplicit Backendの現状挙動を壊していない。

### Phase 1: Editor移行に不足する共通部品を確定

目的: Panel移植中に場当たり的なWidgetやBackend依存を増やさない。

- [ ] Menu Bar / Menu / Menu Item、Collapsible Section、Separator等、Editor shellとInspectorに必要な最小Widgetを棚卸しする。
- [ ] `UIImmediateContext`に追加するものとRetained Widgetとして追加するものを分類する。
- [x] Scene/Game View用Imageに`UITextureView`を追加し、RenderTarget参照、UV向き、Aspect Fit、Clip、DPI/Texture Pixel変換を定義する（契約は`docs/raven-ui-render-target-image-contract.md`）。
- [ ] Editor向けShortcut routingを定義し、Text入力中、Popup表示中、Gizmo操作中の優先順位を決める。
- [ ] Hover / Focus / Active / CaptureをPanelまたはViewport単位で問い合わせるAPIを確定する。
- [ ] Dock layoutのversion、既定配置、破損時fallback、旧`imgui.ini`から移行しない場合の初期化方針を決める。
- [ ] Editor共通Styleを`UITheme`へ追加し、個別PanelへMagic Numberを散らさない。

Exit Gate:

- 仮のEditor shell内に、Menu、Dock Pane、Table/Tree、Input、RenderTarget Imageを並べられる。
- OpenGL / DirectX 12 / VulkanでTexture参照がUI Coreへnative handleを漏らさない。
- FocusとCaptureの自動テストがある。

### Phase 2: 小規模Debug Overlayの移行

対象候補:

- `CharacterPositionDebugOverlayLayer`
- `CharacterLocomotionDebugOverlayLayer`
- `FluidBuoyancyDebugOverlayLayer`
- その他、値表示・Checkbox・Slider中心のDebug UI

方針:

- `UIImmediateContext`を使い、毎Frameの診断値表示と調整値操作を移す。
- Runtime設定と一時的な表示状態を分離する。
- Overlay LayerはSceneを所有せず、Active Scene変更時に参照を再解決する。
- 移行済みLayerから`OnImGuiRender()`と`imgui.h`依存を削除する。

Exit Gate:

- 対象Overlayの表示・操作・Scene切替後の再接続がRaven UIで動作する。
- UIを非表示にした際に不要なRuntime処理やDraw Commandを発行しない。
- 移行対象のImGui API参照が0件になる。

### Phase 3: 読み取り中心Panelの移行

優先順:

1. Statistics
2. Animation Debugの読み取り部分
3. Animation Debugの操作部分

方針:

- Profiler、Renderer、Physics、Animationのデータを表示用Snapshotへまとめる。
- Table/Treeの仮想化または表示行数制限を用意し、大量データでWidgetを無制限生成しない。
- 「値なし」「無効」「前Frameの値」を区別して表示する。
- CPU ProfilerのEnabled切替等、書き込み操作は明示的Actionへ分離する。

Exit Gate:

- ImGui版と同じ診断項目を表示し、数値が同一FrameのRuntime状態と一致する。
- Profiler無効時、Sceneなし、Animationなしでも安全に表示できる。
- 大量行でCPU時間とDraw Command数を計測し、許容上限を記録する。

### Phase 4: Scene HierarchyとInspectorの移行

目的: Editorの選択・編集フローをRaven UIへ移す。

- [ ] Scene Hierarchy用View Modelを作り、Entity ID / Generation / 親子関係 / 表示名をSnapshot化する。
- [ ] 選択状態は`EditorLayer`に一元化し、Hierarchy、Inspector、Viewport、Gizmoで共有する。
- [ ] Create / Delete / Rename / Reparentを既存または追加のEditor Commandへ接続する。
- [ ] InspectorのComponent表示をSection単位へ分け、UIコードとComponent変更処理を分離する。
- [ ] 連続値編集は「Drag中Preview」と「操作完了時のCommand確定」を分ける。
- [ ] Entity破棄、Scene交換、Index再利用時に選択と編集中状態を破棄する。
- [ ] Hierarchy Drag & Dropへ循環防止、同一親内移動、Undo/Redoを接続する。

Exit Gate:

- Entity選択、Rename、Transform編集、Component編集、Undo/Redoが一致する。
- Scene変更、Entity削除、Undoによる復元後もdangling参照を保持しない。
- Text入力中のShortcut、IME確定、Focus移動が期待どおり動作する。

### Phase 5: Scene View / Game View / Gizmoの移行

このPhaseは最も不具合影響が大きいため、機能を分割して進める。

#### 5.1 Viewport表示

- [ ] Scene ViewとGame ViewのFramebufferをRaven UI Imageへ表示する。
- [ ] 論理DIP、Framebuffer Pixel、RenderTarget Pixelの3座標系を明記して変換する。
- [ ] OpenGLの上下反転をWidgetへ直書きせず、Texture ViewまたはRenderer側の規約で扱う。
- [ ] Resizeは0サイズ、最小化、DPI変更、1Frame遅延を安全に処理する。

#### 5.2 PickingとCamera入力

- [ ] Imageの実表示矩形からPicking Attachment座標へ正規化変換する。
- [ ] LetterboxやPaddingがある場合は映像外Clickを除外する。
- [ ] Scene ViewのHover / Focus中だけEditor Camera入力を許可する。
- [ ] Raven UIが消費した入力をRuntime Sceneへ伝播しない。

#### 5.3 Gizmo

- [ ] Translate / Rotate / Scaleの状態機械と描画生成をImGui DrawListから分離する。
- [ ] Gizmo primitiveはRaven UI DrawListまたはEditor debug primitive rendererへ出力する。
- [ ] 軸Hit Test、Drag plane、World/Local座標、Transform積順をテスト可能な計算へ分ける。
- [ ] Drag開始時のTransformを保持し、Drag完了時に1つのUndo Commandとして確定する。
- [ ] GizmoがMouseをCaptureしたFrameはEntity PickingとCamera操作を抑止する。

Exit Gate:

- 3 BackendでScene/Game Viewが正しい向き・Aspect・Clipで表示される。
- Window resize、Dock resize、別DPI Monitor移動、補助Window分離後もPicking位置が一致する。
- Gizmoの3 Mode、Undo/Redo、Entity削除、Scene交換、Focus喪失を確認する。

### Phase 6: Editor shellとDockingの切替

目的: ImGui DockSpace、Menu Bar、Window visibility管理をRaven UIへ置き換える。

- [ ] `EditorLayer`所有のEditor UI Rootと`UIDockSpace`を正式経路にする。
- [ ] Scene View / Game View / Hierarchy / Inspector / Statistics / Animation DebugをDock Tabとして登録する。
- [ ] View MenuからPanel表示状態を一元管理する。
- [ ] 既定レイアウトをコードまたはversion付き設定として定義する。
- [ ] Layout保存は一時ファイル、置換、backup、破損時fallbackを利用する。
- [ ] Tab切り離し、補助OS Window、Close時の元Pane復帰をEditor実データで検証する。
- [ ] Panelの表示状態とPanel内部状態を分け、Layout再構築でRuntime編集状態を意図せず失わない。

Exit Gate:

- ImGui DockSpaceを生成せず、Raven UIだけでEditor全体を操作できる。
- 初回起動、再起動、破損Layout、Monitor間移動、補助Window Closeを確認する。
- Menu、Shortcut、Dock Drag、Viewport Dragの入力優先順位が一意である。

### Phase 7: Raven UIを既定経路にして安定化

- [ ] 通常Editor起動をRaven UI既定にする。
- [ ] Dear ImGui経路は明示的な緊急fallbackだけに限定し、新機能追加を停止する。
- [ ] 少なくとも複数の通常開発セッションで、Editor操作、Scene切替、長時間実行、Multi-Viewportを確認する。
- [ ] CPU Frame時間、UI Draw Command、Vertex/Index、Texture切替、Allocationを計測する。
- [ ] Debug/Release、OpenGL/DirectX 12/Vulkan、100%/高DPIで回帰確認する。
- [ ] ImGui版との機能差を一覧化し、削除を妨げる差分だけを解消する。

Exit Gate:

- fallbackを使わず主要な開発作業を完遂できる。
- Crash、入力不能、Layout消失、編集結果不一致の既知Blockerが0件である。
- Performance予算を超える場合は、Phase 8前に原因と対策を記録する。

### Phase 8: Dear ImGuiの完全削除

削除順は依存の外側から行う。

1. [ ] 各Layerの`OnImGuiRender()` overrideを削除する。
2. [ ] `Layer::OnImGuiRender()`を削除し、必要ならRaven UI向けの明示的Lifecycle名へ置き換える。
3. [ ] `Application`のImGui Frame Loop、`m_ImGuiLayer`、`EnableDearImGui`を削除する。
4. [ ] `Raven/ImGui/ImGuiLayer.h/.cpp`を削除する。
5. [ ] `.vcxproj` / `.vcxproj.filters`からImGui source、include path、Filterを削除する。
6. [ ] `vendor/imgui` submoduleと`.gitmodules`の該当設定を削除する。
7. [ ] `imgui.ini`と移行専用fallback設定を削除する。
8. [ ] コメント、ドキュメント、テスト名に残る古い前提を更新する。
9. [ ] `Root/Root/Raven`以下の構成変更に合わせて`structure.txt`を更新する。

Exit Gate:

- `rg -n --glob '!*.md' "ImGui|imgui" Root/Root/Raven Root/Root/main.cpp Root/Root/Root.vcxproj Root/Root/Root.vcxproj.filters`が0件である。
- submoduleを取得しない新規checkoutからConfigure / Buildできる。
- Raven UI CPUテスト、GPU統合テスト、通常アプリ、Editor Smoke Testが成功する。
- Debug/Releaseの生成物にImGui objectやsymbolが含まれない。

## 7. 推奨するPR分割

1つの巨大PRにせず、原則として次の単位に分ける。

1. 移行スイッチとBaseline計測
2. 不足Widget / Editor Style / RenderTarget Image契約
3. Character / Fluid等のDebug Overlay
4. Statistics Panel
5. Animation Debug Panel
6. Scene Hierarchyの表示と選択
7. Hierarchy編集とUndo/Redo
8. Inspectorの読み取り表示
9. Inspector編集とUndo/Redo
10. Scene/Game View表示
11. PickingとEditor Camera入力
12. Translate / Rotate / Scale Gizmo
13. Editor DockSpace / Menu / Layout永続化
14. Raven UI既定化と安定化
15. ImGui Runtime・project・submodule削除

各PRでは、対象機能のImGui依存を削除できるなら同じPR内で削除する。新旧実装を長期間二重保守しない。

## 8. テスト戦略

### 8.1 CPU自動テスト

- Layout、Clip、Hit Test、Focus、Capture、Popup、Drag & Drop、Docking。
- Entity selectionのGeneration検証とScene交換。
- Viewport座標変換。DIP、Framebuffer Pixel、RenderTarget Pixel、上下反転を組み合わせる。
- Gizmo Hit TestとTransform計算。World/Local、親Transformあり、非一様Scaleを含める。
- Inspector編集のCommand生成、連続編集の集約、Undo/Redo。
- Layout JSONのversion、破損、backup、上限サイズ、未知Panel。

### 8.2 GPU統合テスト

- Font Atlas、Image、RenderTarget Texture、Scissor、Blend、Texture切替。
- OpenGL / DirectX 12 / Vulkanで同じ`UIDrawList`が描画できること。
- Scene ViewとUIの描画順、Resize後Resource、補助Windowごとの描画Target。
- 可能なら小さな参照画像による向き、Clip、色、DPIの比較。

### 8.3 手動Smoke Test

- 起動、終了、最小化、Resize、Alt-Tab、Focus喪失。
- Scene/Game View切替、Dock resize、Tab移動、補助Window分離・復帰。
- Entity選択、Rename、Transform編集、Component編集、Undo/Redo。
- Gizmo Drag中のViewport外移動、Mouse Up取りこぼし、Escape Cancel。
- Text入力、Clipboard、IME、日本語、Shortcut競合。
- 100%と高DPI、異DPI Monitor間移動。
- Scene切替中のFade / Loading Overlayと入力ブロック。

### 8.4 Performance確認

最低限、次をImGui版Baselineと比較する。

- UI CPU Update / Layout / DrawList生成時間。
- UI Draw Call、Vertex、Index、Texture切替数。
- Frameごとの一時Allocationと最大保持Widget数。
- 大規模Hierarchy、Profiler大量Scope、Table大量行での応答性。

## 9. 主なリスクと対策

| リスク | 影響 | 対策 |
| --- | --- | --- |
| ImGuiとRaven UIの二重入力 | Click貫通、二重編集、Shortcut競合 | Panel単位で入力所有者を1つにし、重なる比較表示を操作不可にする |
| Retained WidgetがRuntime参照を保持 | Scene交換・Entity削除後のdangling参照 | Snapshot、Generation検証、Scene変更通知、Frame境界での再接続 |
| Viewport座標系の混同 | Pickingずれ、Gizmoずれ、上下反転 | DIP / framebuffer / render targetを型または明示関数で分離し数値テストする |
| Inspectorの連続編集 | Undo履歴爆発、途中値の確定 | PreviewとCommitを分け、操作単位でCommandを1つに集約する |
| Docking移行時のLayout消失 | Editor作業性低下 | version付き保存、backup、既定layout fallback、旧形式は読み捨て方針を明示 |
| UI RendererのBackend差 | OpenGLだけ成功しExplicit Backendで欠落 | Widget実装でnative handleを扱わず、各PhaseのExit Gateに3 Backendを含める |
| 大量Retained Widget | CPU・Memory増加 | Tree/Tableの仮想化、Snapshot差分更新、Profiler計測 |
| 一括削除の切り戻し困難 | Editor利用不能 | Phase 7の安定化後に、Runtime・project・submoduleを分割して削除する |

## 10. 直近の推奨作業

次の着手点はPhase 0とPhase 1である。

1. 現在のImGui依存一覧と主要Editor操作のBaselineを固定する。
2. Panel単位のBackend選択を導入し、二重入力を防ぐ。
3. Scene/Game View用RenderTarget ImageのBackend非依存契約を設計・テストする。
4. Menu / Collapsible Section等の不足Widgetを最小限だけ追加する。
5. 最初の移行対象としてCharacterまたはFluidの小規模Debug Overlayを選ぶ。
6. その移行で得たLifecycle・入力・Styleの知見をStatistics Panelへ展開する。

最初から`EditorLayer`全体を書き換えない。小規模Overlayで移行規約を確定し、読み取り中心Panel、編集Panel、Viewport、Docking shellの順にリスクを上げるのが安全である。

## 11. 進捗記録テンプレート

各PhaseまたはPR完了時に、最低限次を追記する。

| 日付 | PR / Commit | 対象 | 削除したImGui依存 | ビルド・テスト | 手動確認 | 残課題 |
| --- | --- | --- | --- | --- | --- | --- |
| YYYY-MM-DD | `feature/...` | Panel名 | file / call数 | 構成と結果 | 操作項目 | 次Phaseへ持ち越す内容 |

完了報告では「実装済み」「自動テスト済み」「実環境で動作確認済み」を区別する。未検証項目を完了扱いにしない。
