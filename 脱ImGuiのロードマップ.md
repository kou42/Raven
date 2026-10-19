# Raven 脱ImGuiロードマップ

最終更新: 2026-10-19

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

- [x] Menu Bar / Menu / Menu Item、Collapsible Section、Separator等、Editor shellとInspectorに必要な最小Widgetを棚卸しする（`docs/raven-editor-ui-common-parts.md`）。
- [x] `UIImmediateContext`に追加するものとRetained Widgetとして追加するものを分類する（`docs/raven-editor-ui-common-parts.md`）。
- [x] Scene/Game View用Imageに`UITextureView`を追加し、RenderTarget参照、UV向き、Aspect Fit、Clip、DPI/Texture Pixel変換を定義する（契約は`docs/raven-ui-render-target-image-contract.md`）。
- [x] Editor向けShortcut routingを定義し、Popup、Text入力、Gizmo、Viewport、Panel、Globalの優先順位を`EditorShortcutRouter`へ集約する。
- [x] `UIContext::GetInteractionState()`でHover / Focus / Active / CaptureをPanelまたはViewport単位で問い合わせられるようにする。
- [x] Dock layoutのversion、既定配置、Primary→Backup→既定配置のfallback、旧`imgui.ini`から移行しない初期化方針を定義する（`docs/raven-editor-dock-layout.md`）。
- [x] Editor共通Styleを`UITheme::Editor`へ追加し、Menu / Collapsible Section / Separatorの共通寸法・装飾色を集約する。

Exit Gate:

- 仮のEditor shell内に、Menu、Dock Pane、Table/Tree、Input、RenderTarget Imageを並べられる。
- OpenGL / DirectX 12 / VulkanでTexture参照がUI Coreへnative handleを漏らさない。
- FocusとCaptureの自動テストがある。

### Phase 2: 小規模Debug Overlayの移行

対象候補:

- [x] `CharacterPositionDebugOverlayLayer`
- [x] `CharacterLocomotionDebugOverlayLayer`
- [x] `FluidBuoyancyDebugOverlayLayer`
- その他、値表示・Checkbox・Slider中心のDebug UI

方針:

- 毎Frameの診断値だけで完結するOverlayは`UIImmediateContext`を第一候補にする。移動・Resize・Scrollや複数の連続操作状態を持つ既存3 HUDは、LifetimeをWidget Treeへ集約するRetained UIとして移行済み。
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

- [x] StatisticsのRuntime、Profiler、Renderer、Physics、Astroデータを`StatisticsSnapshot`へまとめる。
- [x] StatisticsをRaven UI Panelへ接続し、Dual / Raven UIモードの表示所有権をRaven UIへ移す。
- [x] AnimationのState Machine、Locomotion、Trajectory、Bone診断データを`AnimationDebugSnapshot`へまとめる。
- [x] Animation Debugの読み取り専用Raven UI Panelを追加し、Raven UIモードへ接続する。
- [x] Animation DebugのMode切替ActionをRaven UIへ移し、Dualモードの所有権をRaven UIへ切り替える。
- [x] `UITable`の外部DataSourceと可視行範囲による仮想化を用意し、100万行でもWidgetや文字列を全件生成しない。
- [x] StatisticsのFrame Timing、Physics、CPU Profilerで「値なし」「無効」「前Frameの値」を区別して表示する。
- [x] CPU ProfilerのEnabled切替を明示的Actionへ分離し、ImGui版とRaven UI版で共有する。

Exit Gate:

- ImGui版と同じ診断項目を表示し、数値が同一FrameのRuntime状態と一致する。
- Profiler無効時、Sceneなし、Animationなしでも安全に表示できる。
- 大量行でCPU時間とDraw Command数を計測し、許容上限を記録する。
  - Debug x64の自動テストでは100万行の`UITable`を対象に、1 Frame 50ms以内、セル問い合わせ3回以内、Draw Command 8件以内を回帰上限とする。

### Phase 4: Scene HierarchyとInspectorの移行

目的: Editorの選択・編集フローをRaven UIへ移す。

- [x] Scene Hierarchy用View Modelを作り、Entity ID / Generation / 親子関係 / 表示名をSnapshot化する。
- [x] 選択状態は`EditorLayer`に一元化し、Hierarchy、Inspector、Viewport、Gizmoで共有する。
- [x] Create / Delete / Rename / Reparentを既存または追加のEditor Commandへ接続する。
  - [x] Createを既存`CreateEntityCommand`へ接続する。
  - [x] Renameを既存`RenameEntityCommand`へ接続し、Enter / Focus喪失時に1 Commandとして確定する。
  - [x] DeleteのComponent復元SnapshotとCommandを追加する（標準8種のComponentを対象とし、未知のComponentは削除を禁止。Undo時の新Generation割当と履歴Handle再割当を回帰テストで確認）。
  - [x] 親子関係の正規Runtimeデータを定義してReparent Commandへ接続する（RelationshipComponent、Scene API、ReparentEntityCommand、Hierarchy Snapshot、Raven UI Drag/Drop、循環防止、Undo/Redoを回帰テストで確認）。
- [x] InspectorのComponent表示をSection単位へ分け、UIコードとComponent変更処理を分離する（Raven UI読み取り表示をSnapshot境界で追加。編集Actionは次項で移行）。
- [x] 連続値編集は「Drag中Preview」と「操作完了時のCommand確定」を分ける。
  - [x] Raven UI版Transform数値入力をPreviewとEnter / Focus喪失時の1 Command確定へ分離する。
  - [x] Raven UI版Camera Projection入力を`SetPerspective()`経由のPreviewと1 Command確定へ分離する。
  - [x] Camera Primary、RigidBody、ColliderのRaven UI編集Actionを接続する（PrimaryはScene全Camera状態を復元する専用Command、連続値はPreviewと1 Command確定、型・boolは即時Commandとして実装）。
- [x] Entity破棄、Scene交換、Index再利用時に選択と編集中状態を破棄する。
- [x] Hierarchy Drag & Dropへ循環防止、同一親内移動、Undo/Redoを接続する。

Exit Gate:

- Entity選択、Rename、Transform編集、Component編集、Undo/Redoが一致する。
- Scene変更、Entity削除、Undoによる復元後もdangling参照を保持しない。
- Text入力中のShortcut、IME確定、Focus移動が期待どおり動作する。

### Phase 5: Scene View / Game View / Gizmoの移行

このPhaseは最も不具合影響が大きいため、機能を分割して進める。

#### 5.1 Viewport表示

- [ ] Scene ViewとGame ViewのFramebufferをRaven UI Imageへ表示する。
  - [x] OpenGL FramebufferのColor Attachmentを`Ref<Texture>`でRaven UIへ接続する。
  - [x] DirectX 12 / VulkanのEditor用off-screen RenderTargetを`RHITexture`で接続する。
    - [x] Backend共通の`RHIOffscreenRenderTarget`契約とDevice / Command Listの拡張口を追加する。
    - [x] Raven UI Viewport PanelがColor Attachmentの`Ref<RHITexture>`を直接保持・表示する。
    - [x] DX12 / Vulkanのnative RenderTarget生成、状態遷移、Scene描画を接続する。
      - [x] DX12のRGBA8 Color + D32 Depth、RTV / SRV、描画前後Barrierを実装する。
      - [x] DX12 Explicit RuntimeでScene/Game Viewの描画SnapshotをTargetへ記録する。
      - [x] VulkanのImage / ImageView / Framebuffer、Layout遷移を実装する。
- [x] 論理DIP、Framebuffer Pixel、RenderTarget Pixelの3座標系を明記して変換する。
- [x] OpenGLの上下反転をWidgetへ直書きせず、Texture ViewまたはRenderer側の規約で扱う。
- [x] Resizeは0サイズ、最小化、DPI変更、1Frame遅延を安全に処理する。

2026-10-18時点で、OpenGLのRaven UI / Dual経路にScene ViewとGame Viewを接続した。
`EditorViewportGeometry`がWindow論理座標と実Framebuffer寸法からRenderTarget Pixel数を算出し、
`EditorViewportRavenPanel`が`UITextureOrigin::BottomLeft`付きの`UITextureView`へColor Attachmentを渡す。
Explicit Backend向けには`RHIOffscreenRenderTarget`が寸法不変TargetとSample可能なColor Attachmentの所有境界を定義し、
`EditorViewportRavenPanel`はTopLeft原点の`Ref<RHITexture>`をnative handleへ変換せず表示できる。
DX12は同じColor ResourceへRTVとSRVを作り、同一Command List内でSwapChain Passを一時停止して
`PIXEL_SHADER_RESOURCE`と`RENDER_TARGET`を往復できる。ResizeではTarget全体を交換し、旧Targetは
UI DrawListとFrame Fenceの参照が完了するまでContextが保持する。
DX12 RuntimeはMain SceneのQueueをEditor再描画前にSnapshot化し、Scene View / Game Viewを別Snapshotとして保持する。
同一Command List内でSwapChain Passを一時停止して各off-screen Targetへ描画し、TargetをSampling可能状態へ戻した後に
Raven UIをSwapChainへ描画する。Target ResizeはRef全体の交換とFrame Fence保持を維持する。
VulkanはSwapChainと互換なColor形式のImage / ImageView / SamplerとD32 Depth、Framebufferを生成し、
同一Command Buffer上で`SHADER_READ_ONLY_OPTIMAL`と`COLOR_ATTACHMENT_OPTIMAL`を往復する。
off-screen描画中だけSwapChain RenderPassを中断し、LOAD互換RenderPassでMain SceneのColor / Depthを保持したまま
Raven UI描画へ復帰する。TargetはFrame FenceまでContextが保持し、SwapChain Resize時は借用RenderPassより先に
native FramebufferとColor Textureを無効化して、Editor側が次Frameに新Targetへ交換する。
DX12 / Vulkan実機Smoke Testが未完了のため、Phase 5.1のExit Gateは未達である。

#### 5.2 PickingとCamera入力

- [x] Imageの実表示矩形からPicking Attachment座標へ正規化変換する。
- [x] LetterboxやPaddingがある場合は映像外Clickを除外する。
- [x] Scene ViewのHover / Focus中だけEditor Camera入力を許可する。
- [x] Raven UIが消費した入力をRuntime Sceneへ伝播しない。

2026-10-18時点で、`EditorViewportInputRegion`が`UIImage`のAspectFit実表示矩形を使い、
OpenGL Color Attachmentと同寸のPicking Attachment Pixelへ左上UI座標を変換する。
Letterbox ClickはEditor UI内で消費しつつPicking対象外とし、実映像上の操作だけがFocusと
Mouse Captureを取得する。Editor CameraのInput pollingはScene ViewのHoverとFocusが両方成立する
場合だけ有効化し、Navigation KeyのCore EventとViewport Mouse EventはRuntimeへ伝播させない。

#### 5.3 Gizmo

- [x] Translate / Rotate / Scaleの状態機械と描画生成をImGui DrawListから分離する。
- [x] Gizmo primitiveはRaven UI DrawListまたはEditor debug primitive rendererへ出力する。
- [x] 軸Hit Test、Drag plane、World/Local座標、Transform積順をテスト可能な計算へ分ける。
  - [x] 軸Hit Test、World/Local座標、RotationのQuaternion積順をUI非依存関数へ分離する。
  - [x] TranslateをScreen投影軸方式からCamera RayとDrag Planeの交点方式へ移行する。
- [x] Drag開始時のTransformを保持し、Drag完了時に1つのUndo Commandとして確定する。
- [x] GizmoがMouseをCaptureしたFrameはEntity PickingとCamera操作を抑止する。

2026-10-18時点で、Raven UI経路は`EditorTransformGizmoController`がTranslate / Rotate / Scaleの
状態、Hit Test、Snap、World / Local空間、Preview、Commit / Cancelを所有する。
ControllerはGPUやWidgetを参照せずViewport local座標のPrimitive Snapshotを生成し、
`EditorGizmoOverlay`がRaven UIのPolygon / Circle / Rectへ変換する。Viewport入力領域はGizmoを
Pickingより先に判定し、Drag中はMouse Captureを保持してEditor CameraとRuntimeへの伝播を止める。
既存Dear ImGui経路は比較・fallback用として維持している。Translateの操作量はCamera Rayと、選択軸を
含みCamera視線に正対するDrag Planeとの交点差から求める。Camera視線と選択軸がほぼ平行で安定した
Drag Planeを構成できない退化ケースのみ、従来のScreen投影軸方式へfallbackする。

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

次の着手点はPhase 5.1～5.3の3 Backend実機Smoke Testと、Explicit BackendのPicking対応である。

1. OpenGL / DX12 / VulkanでScene/Game Viewの向き、Aspect、Clip、Main Sceneとの同時更新を実機確認する。
2. 100% / 高DPI、Dock / Window / SwapChain Resize、最小化復帰でTarget再生成後の表示を確認する。
3. DX12 Debug LayerとVulkan Validation LayerでState / Layout / RenderPass / Descriptor / Lifetime Errorがないことを確認する。
4. OpenGL Scene ViewでPicking、Letterbox除外、右Mouse Camera、Runtime入力遮断、Gizmoを実機確認する。
5. DX12 / VulkanのScene ViewへPicking ID Attachmentと非同期Readbackを追加する。
6. 3 Backend共通のViewport座標・Picking・Gizmo回帰を確認してPhase 5のExit Gateを閉じる。

Phase 4のInspector編集は自動テストまで完了しているが、Raven UIを使った実機操作は未確認である。Phase 5へ進む際も、Viewport表示、入力、Gizmoを同時に置き換えず、座標系と入力所有権を段階ごとに固定する。

## 11. 進捗記録テンプレート

各PhaseまたはPR完了時に、最低限次を追記する。

| 日付 | PR / Commit | 対象 | 削除したImGui依存 | ビルド・テスト | 手動確認 | 残課題 |
| --- | --- | --- | --- | --- | --- | --- |
| YYYY-MM-DD | `feature/...` | Panel名 | file / call数 | 構成と結果 | 操作項目 | 次Phaseへ持ち越す内容 |
| 2026-10-18 | 作業ツリー | Phase 5.1 OpenGL Viewport表示 | DualのScene/Game View表示所有権をRaven UIへ移行 | Debug x64 build成功（PDB無効）、Viewport座標Self Test成功、Raven UI CPU Testは既存Dock保存権限エラーで失敗 | 未実施 | OpenGL実機Smoke Test、Phase 5.2、DX12/Vulkan off-screen RenderTarget |
| 2026-10-18 | 作業ツリー | Phase 5.2 Picking / Camera入力 | Raven UI ViewportがMouseとCamera Navigation Keyを所有 | Debug x64 build成功（PDB無効）、Viewport入力・座標Self Test成功 | 未実施 | OpenGL実機Smoke Test、Phase 5.3 Gizmo、DX12/Vulkan off-screen RenderTarget |
| 2026-10-18 | 作業ツリー | Phase 5.3 Raven UI Gizmo | Translate / Rotate / Scaleの状態・描画・入力をImGuiから分離 | Debug x64 build成功（PDB無効）、3 Modeの操作・Undo、Capture優先度、Capture Cancel、Ray / Drag Plane・軸平行fallback Self Test成功 | 未実施 | OpenGL実機Smoke Test、DX12/Vulkan off-screen RenderTarget |
| 2026-10-18 | 作業ツリー | Phase 5.1 Explicit off-screen共通境界 | RHI RenderTargetの所有・寸法変更時のTarget交換・Color Attachment公開契約とRaven UI表示経路を追加 | Debug x64 build成功（PDB無効）、RHI Texture表示・TopLeft座標・Specification Self Test成功 | 未実施 | DX12/Vulkan native RenderTarget生成、状態遷移、Scene描画、Picking |
| 2026-10-18 | 作業ツリー | Phase 5.1 DX12 off-screen native target | RGBA8 + D32 Target、RTV / SRV、Barrier、SwapChain Pass復帰、Fence Lifetimeを追加 | Debug x64 build成功（PDB無効）、Viewport・Entity Command Self Test成功 | 未実施 | DX12 Viewport別Scene記録、Vulkan native target、Picking |
| 2026-10-19 | `feature/dx12-editor-viewport-snapshots` | Phase 5.1 DX12 Viewport Snapshot | Main / Scene View / Game Viewの描画Snapshotを分離し、RHI TargetとRaven UI Imageへ接続 | 未実施（GitHubコネクタ環境ではMSBuild不可） | 未実施 | DX12実機Smoke Test、Vulkan native target、DX12 Picking |
| 2026-10-19 | PR #309 | Phase 5.1 Vulkan Viewport Snapshot | VkImage / ImageView / Sampler / D32 / Framebuffer、Layout遷移、SwapChain Pass復帰、Fence Lifetime、Resize後Descriptor整理を追加 | Vulkan変更cppのGCC C++17構文コンパイル成功、`git diff --check`成功 | 未実施 | DX12 / Vulkan実機Smoke Test、Explicit Picking ID Attachment |

完了報告では「実装済み」「自動テスト済み」「実環境で動作確認済み」を区別する。未検証項目を完了扱いにしない。
