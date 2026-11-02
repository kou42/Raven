# 脱ImGui Phase 7 Windows自動検証記録

検証日: 2026-11-01～2026-11-02（JST）

対象コミット: `241a5cd9954caec545aa1601d39dfd3f0144b34f`

## 範囲と環境

今回のマイルストーンはWindowsの通常ビルドと既存自動テストの検証に限定する。
Phase 7全体の完了、ImGui削除、Editor実操作の合格を意味しない。

- Visual Studio 2022 Community、MSVC v143（14.44.35207）。
- Vulkan SDK: `C:\VulkanSDK\1.4.357.0`。CI指定の1.4.363.0とは異なる。
- GPU: NVIDIA GeForce RTX 3080、Driver 32.0.16.1060。
- x64、既存依存物を使い `-SkipBootstrap` を指定。Clean Buildは行っていない。
- 一部ビルドではプロセス内の `CL=/MP4` でコンパイルを並列化した。
- 既存の `.gitignore`、workspace削除、Dock layout等の作業差分は保持した。

## 結果

| 検証 | 終了コード | 結果 |
| --- | --- | --- |
| Debug通常アプリ | 0 | ビルド成功 |
| Release通常アプリ | 0 | ビルド成功 |
| Debug CPU UI | 0 | ビルド・テスト成功 |
| Release CPU UI | 0 | ビルド・テスト成功 |
| Debug Allocation計測 CPU UI | 0 | ビルド・テスト成功 |
| Debug OpenGL GPU統合（Font Atlas必須） | 0 | ビルド・テスト成功、SKIPなし |

通常ビルドは `scripts/build-windows.ps1 -Configuration Debug/Release -SkipBootstrap`、
CPUは `scripts/test-windows-ui.ps1 -Configuration Debug/Release -SkipBootstrap`、
計測モードはCPUのDebugに `-AllocationProfiling` を追加した。
GPUは `scripts/test-windows-gpu.ps1 -Configuration Debug -RequireFontAtlas -SkipBootstrap` を実行した。
GPU出力の `[PASS] GPU Texture upload diagnostics` を確認した。
CPUテストは件数を出力しないため、成功件数は集計せず終了コード0で判定する。

CPUテストには性能記録Panelの開始・停止・保存・非表示・Context間移譲・再Attach、
Window別集計、未計測値、CSV、Allocation契約の回帰が含まれる。
GPUテストの成功はOpenGL Texture upload/readbackとFont Atlasの検証であり、
DX12/VulkanのEditor描画・Pickingの実機合格には代用しない。

## 実行制限と警告

制限内ではDebug通常/CPUのリンクで `LNK1101`（MSPDB140.DLL不整合）、
Releaseコンパイルで `C1902`、GPU事前確認でCIMアクセス拒否が発生した。
承認を得て制限外で再実行すると成功した。実行制限との関連が疑われるが、根本原因は未確定。
PDBを無効化する診断設定や製品コードの変更は使用していない。

ビルドスクリプトが報告した警告行数は、通常Debug 20、通常Release 0、
CPU Debug 84、CPU Release 64、Allocation Debug 84、GPU Debug 20。
主な種類は既存コードの `C4244` とDebugの `LNK4098`。
複数行診断・末尾要約の重複を含むため、独立した警告件数とは扱わない。
後続のインクリメンタル再実行で警告0でも、これらを解消した意味にはしない。

## ログ保存の修正

`scripts/test-windows-ui.ps1` はCPU実行結果が同名ビルドログを上書きしていたため、
実行結果を `artifacts/logs/windows-ui-test-results-*.log` へ分離した。
標準出力が空の成功時もログを生成し、終了コードを記録する。
変更後にDebug/Release/AllocationのCPUテストを再実行して確認した。
CIは既存の `artifacts/logs/*.log` 収集で両方を保存できる。

生ログはローカルの `artifacts/logs/` に保存した（Git管理外）。
最初のCPUビルドの診断は `phase7-cpu-Debug-retry.log`、
`phase7-cpu-Release-retry.log`、`phase7-allocation.log` にも保存した。

## 未検証と次のマイルストーン

- 複数の通常開発セッション、Scene切替、長時間実行、補助OS Windowの切り離し・Close・復帰。
- Debug/Release × OpenGL/DX12/Vulkan × 100%/高DPIのEditor実操作。
- 同条件のScene/Panel/操作による性能CSV Baselineと予算判定、ImGui版との差分再棚卸し。
- 今回の変更に対するGitHub Actions実行結果。

次はBackend/DPI別のEditor操作と性能Baseline採取を、別マイルストーンとして承認後に行う。

## 2026-11-02 OpenGL Release実機検証: 起動Blocker

承認範囲: 通常Release Editorの編集操作、Statistics記録、補助Window、15分継続動作、CSV採取。

- `Root/x64/Release/Root.exe --backend=opengl` を `Root/Root` 作業ディレクトリで起動した。
- 起動ログでNVIDIA RTX 3080、OpenGL 3.3.0 NVIDIA 610.60、Game Scene/Terrain読み込みを確認。
- 2回のWindow観察でMenu/Tab/Panelの文字が表示されない。中央に白い矩形も表示される。
- `main.cpp` は `ApplicationSpecification::RuntimeUIFont` を設定せずApplicationを構築する。
  Applicationはその参照を保持し、EditorLayerは各Panelへ `GetRuntimeUIFont()` を渡す。
  `UILabel::OnBuildDrawList()` はFontがnullなら描画しないため、文字非表示の経路が確認できた。
- 白い矩形の原因、実効DPI、15分の安定性は未確定。編集/Undo/Redo、記録ボタン、Tab移動、
  CSV採取は操作対象を判別できないため未実施。性能Baselineの数値は生成していない。
- 起動ログ: `artifacts/logs/phase7-editor-release-stdout.log` / `phase7-editor-release-stderr.log`。
- 検証用WindowへAlt+F4を送信した。製品コードは変更していない。

検証結果は不合格（文字表示のBlockerあり）。修正には通常起動時の共有Font生成・注入経路が必要。
既存 `UIFontAtlasBuilder` / DPI Cacheを再利用し、GPU Context有効後に生成してPanelへ共有する方針を
追加承認の対象とする。Font生成失敗時の診断と、Debug/Releaseの文字表示再確認も修正範囲に含める。

## 2026-11-02 通常起動Fontの修正と再検証

上記Font修正への追加承認を受けて実施した。

- `ApplicationSpecification::RuntimeUIFontPath` を追加。注入済みAtlasを優先し、
  Atlas未指定時のみWindow/Renderer初期化後に既存 `UIFontAtlasBuilder` で構築する。
- Windows標準Fontのパス選択は `main.cpp` が担当し、`GetWindowsDirectoryW` から
  `Fonts/segoeui.ttf` を指定する。CoreへWindowsパスの知識を持ち込まない。
- AtlasはApplicationがRefで保持し、既存の `GetRuntimeUIFont()` 経由で各Panelへ共有する。
  Font生成失敗時はパスと `UIFontAtlasBuildFailure` の数値を標準エラーへ出す。
  失敗時の描画省略とScene処理継続は維持する。
- `UIDockSpace::SetFont` を追加し、既存TabとSplit/復元後に作るTabBarへ共有Fontを渡す。
  Editor shellから同じAtlasを接続した。
- CPU回帰テストで既存PaneとSplit後のPaneのCaptionが同じAtlasのGlyphを描画すること、
  Font交換/解除の反映を確認した。新規ファイル・ファイル移動はない。

最終差分の検証:

| 検証 | 終了コード | 結果 |
| --- | --- | --- |
| Debug通常ビルド | 0 | 成功、警告行2（LNK4098） |
| Release通常ビルド | 0 | 成功、警告0 |
| Debug CPU UI | 0 | 新規Dock Font回帰を含め成功、警告行66 |
| Release CPU UI | 0 | 新規Dock Font回帰を含め成功、警告行64 |

警告行数は従来どおり複数行診断と要約の重複を含む。CPUの主な警告は既存C4244。
実行ログは `artifacts/logs/phase7-font-build-Debug.log` / `phase7-font-build-Release.log`、
`phase7-font-cpu-Debug.log` / `phase7-font-cpu-Release.log` に保存した。
今回の差分ではAllocation計測テスト/GPU統合テストを再実行していない。

OpenGL Release再起動で `[Raven UI] Runtime font loaded: C:\WINDOWS\Fonts\segoeui.ttf` を確認。
Menu、Panel内の英語、Dock Tabのタイトルが表示された。Tab選択の表示変化も確認した。
Debug実行時の画面確認、Font生成失敗経路の実起動、他Backendは未検証。
収録文字はASCIIと省略/矢印等の操作記号に限定し、未収録文字は代替Glyphで表示する。
日本語名の全文字表示やDPIごとのFont再Rasterizeは今回の修正対象外。

### 残る実機Blocker

Tab名は表示されたが、Statistics/Animation Debugの本文が空のままで記録ボタンに到達できない。
EditorのContentサイズ追従とUITabViewのContent HostのMeasure/Arrange境界を調べる必要がある。
原因はまだ確定していないためLayout修正は行っていない。
中央の白い矩形はFluid DebugのUIPanelで、背景色未指定時のTheme Panel既定値は白。
白背景に明るい文字が重なり読みにくく、Font未生成とは別の表示問題として残す。

起動ログは `artifacts/logs/phase7-font-final-stdout.log` / `phase7-font-final-stderr.log`。
検証用WindowはAlt+F4で終了。性能CSV、15分継続動作、補助Windowの実操作は引き続き未実施。
次はContent表示/LayoutとDebug Panelのコントラストを追加承認後に修正し、実機検証を再開する。

## 2026-11-02 Dock本文Layoutと背景コントラストの修正

追加承認範囲: Tab Contentのサイズ/Layout、Fluid Debugの背景、回帰テストと実機検証。

### 変更と責務

- `UITabView` のHeader/Content Hostを親の自然サイズ集約から除外し、Headerを除く残り高さを
  Hostと選択PageへStretch配分する。空Pageや内部にMeasure対象外Windowを持つPageも0高さにしない。
- `UIElement` のVertical主軸Stretchで固定高・Margin・Spacingを除いた残りを均等配分する。
  AbsoluteのStretchも親の確定領域へ追従させ、幅依存Reflowにも同じ利用可能幅を渡す。
  Min/Maxの制約は既存Clampを維持する。制約でClampされた余りの再分配は行わない。
- EditorでDockへ移したPanelに縦横Stretchを設定する。Statistics内のScroll Viewportも
  Headerと余白を除く実寸へ追従させ、Dock縮小後に本文末尾の操作へScrollできるようにした。
- Fluid Buoyancy DebugのContent背景だけを暗色に指定した。汎用Themeの白い既定値は変更していない。
- 新規ソース/ヘッダー、ファイル移動、削除はないためstructure/filtersの構成更新は不要。

### 自動検証

- Debug/Release通常ビルド: 成功、両方ともビルドスクリプト報告の警告0。
- Debug/Release CPU UI: 成功。追加回帰は本文の表示寸法、同FrameのResize、Mouse Click、
  空Tab切替/復帰、および実Statisticsを小さいDockへ入れてScroll後に記録ボタンをMouseで押す操作。
- Debug Allocation計測CPU UI: 成功。通常CPUの初回再コンパイルは既存C4244等の警告行66/64、
  Allocation計測は警告行64。独立した警告件数ではない。
- 新規テストでHeaderの自然幅が縮小を妨げる問題を検出し、Measureから除外して修正した。
- 追加テストのUIWindow include不足も検出・修正した。いずれも修正後の実行で成功。
- GPU統合は今回の差分では再実行していない。GPU Backendの実装は変更していない。

ログは `artifacts/logs/phase7-layout-build-*.log`、`phase7-layout-cpu-*.log`、
`phase7-layout-allocation.log`。最後の明示的null比較への規約調整後も通常CPUを再実行し、
Debug/Releaseとも終了コード0を確認した（`phase7-layout-cpu-*-final.log`）。

### OpenGL Release実機結果

起動: 2026-11-02 00:39:16 JST、通常Game Scene、RTX 3080、OpenGL 3.3 NVIDIA 610.60、
Dear ImGui fallbackなし。Window寸法表示は1920×1080。OSの実効DPI値は今回採取していない。

- Hierarchy、Inspector、Scene View、Statistics、Animation Debugの本文が表示された。
- HierarchyのFloor選択がInspectorへ反映され、Tabの選択切替も確認。
- StatisticsのScrollで記録ボタンに到達し、開始/停止/保存/再開始を確認。
- View Menuで非表示にして再表示するとSamplesが316から578へ増加し、非表示中の記録継続を確認。
- 停止後はSamples=2334で固定され、CSVが生成された。
- Inspector Position Xの入力は値の保持を確認できず、編集/Undo/Redoは未判定。
  入力経路の問題か自動操作の制約かは未確定であり、このマイルストーンでは修正していない。
- Tab切り離しは自動Dragで再現できず、補助OS Windowは生成されなかった。
  ログではMouse Downが移動先座標で処理されており、入力注入と低Frame Rateのタイミングの可能性がある。
  手動の緩やかなDragによる実機確認を残す。補助Windowの合格/不合格にはしない。
- Scene/Game Viewを一時非表示にしてFluid Debugの暗色背景と文字の可読性を直接確認した。
  その後Scene/Game Viewを再表示し、元のScene→Game順とScene選択へ戻した。
- Game ViewとScene Viewの切替・復帰でも描画が継続し、停止済みSamples=1147は変わらなかった。
- 00:39:16起動から00:54:28以降まで15分以上継続し、Crash/Window消失を観察しなかった。
  最後にAnimation Debug Tabへの入力応答と本文表示を確認し、Alt+F4で終了した。
  1回のセッションの確認であり、複数開発セッションや長時間の一般的な安定性保証とはしない。

### 採取CSV（参考値）

ローカル保存先:

- `artifacts/phase7/opengl-release-operations.csv`: 操作/非表示/ビルド負荷も含む2334サンプル。
- `artifacts/phase7/opengl-release-stationary.csv`: 同じ配置で1分以上静置した1147サンプル。

静置条件はFloor選択、Scene ViewとStatistics記録欄を表示、通常Game SceneのPhysicsを継続。
記録開始/停止時の入力以外のEditor操作とビルドを避けた。通常ReleaseでAllocation計測は無効。

| UI指標 | 平均 | 最大 |
| --- | ---: | ---: |
| Build時間(ms) | 1.1494 | 1.7962 |
| Draw Command | 5118.25 | 5124 |
| Vertex | 20608.01 | 20631 |
| Index | 31123.52 | 31158 |
| Texture切替 | 155 | 155 |
| DrawList確保回数 | 3 | 3 |
| DrawList確保要求bytes | 96 | 96 |
| DrawList保持bytes | 1035936 | 1035936 |
| Element | 421 | 421 |

2 Scope（all_windows_ui / window_1）に各12指標、通常指標1147 samples、C++確保は0 samples/N/A、
平均<=最大を確認した。Main Windowのみなので両Scopeは同じ値。省略Windowサンプルは0。
Build時間はUI構築区間でありScene/Physicsを含む全CPU Frame時間ではない。
全Frame時間は画面で約69～76msの表示を観察しただけで、平均/最大の記録値ではない。
DPI値と比較用予算が未確定のため、Backend/DPI別の正式Baseline合格や性能予算内とは判定しない。

残課題: 入力確定/Undo/Redo、補助Window実操作、Scene切替、DPI採取とBackend/DPI別比較、
計測用ReleaseアプリでのC++ Allocation Baseline。Phase 7のチェックは未完了のまま維持する。

## 2026-11-02 Inspector入力確定・Undo/Redo

承認マイルストーン: 前回入力値を保持できなかった原因の特定と、数値編集の確定/Undo/Redoの検証。

### 原因と変更

- 前回のログでは自動テキスト入力がCtrl+Vで配送されていた。`UIInputText` のClipboard callbackは
  Widgetごとの明示注入しかなく、通常起動側では一度も接続されていなかったため貼り付けが無効だった。
- `UIContext` にClipboard Reader/Writerを追加し、Widgetに個別callbackがない場合は
  入力時点のContextへ委譲する。個別注入の優先順位は維持する。
- ApplicationがMain/補助ContextへGLFWのClipboard APIを接続する。CallbackはWindow IDから
  Registryを引き直し、生のWindow pointerをWidgetへ保持させない。UI CoreへGLFW依存を追加していない。
- Clipboard未接続時はCutで選択文字を削除しない。Context移動後は移動先の接続を利用する。
- Editorの履歴操作がScene ViewのFocusに限定されていたため、既存Shortcut Routerから
  Panel/GlobalのCtrl+Z、Ctrl+Shift+Z、Ctrl+Yを共有Historyへ接続した。
- ModifierはEvent時点のsnapshotを利用する。Text入力/Popup/Gizmoが所有する操作は奪わず、
  Key Repeatでも履歴を連続実行しない。Raven UIでは旧Viewport内の履歴分岐を迂回し、
  Routerが拒否したShortcutを旧経路から再実行しない。
- InspectorのPreview/Commitと既存TransformCommand/History、Scene/Entityの寿命検証は変更していない。
  新規ファイルや構成変更はないためstructure/filtersの追加更新は不要。

### CPU回帰

- Context既定のPaste、別Contextへ移した後のPaste/Cut、未接続Cutでの文字保持、Widget個別注入の優先。
- 実InspectorへPasteし、Runtime PreviewとFocus中のSnapshot更新で入力が保持されること。
- Previewでは履歴なし、Enterは1 Command、Enterの再実行/Focus喪失で重複Commandを追加しないこと。
- Text Focus中にEditor履歴を奪わず、Focusを外した後はViewport FocusなしでUndoできること。
- 1回のUndoで履歴が尽きること、Ctrl+Shift+Z/Ctrl+YのRedo、Focus喪失による2回目の編集確定、
  Gizmo所有中の履歴Shortcut拒否。

Debug/Release CPU UIテスト、Debug/Release通常ビルドはすべて終了コード0。
最終ビルドの警告は両構成0。初回の再コンパイルでは既存C4244/LNK4098を含む
警告行66/64（CPU Debug/Release）、通常Debugの警告行2を報告しており、警告解消とは扱わない。
最終の旧Viewport経路ガード追加後も、両構成のビルドとCPUテストを再実行して成功した。
ログ: `artifacts/logs/phase7-input-build-*-final.log`、`phase7-input-cpu-*-final.log`。

### OpenGL Release実機

通常Game SceneでFloorを選択し、Inspector Position Xを使って確認した。

1. 元値0へASCII `1`を貼り付け、PreviewとFrameを跨いだ保持を確認。
2. Enterで確定し、Inspectorの空白をクリックしてText Focusを外した。
3. Viewportを選び直さずCtrl+Zで0、Ctrl+Shift+Zで1へ戻ることを確認。
4. `2`を貼り付け、Enterを押さず空白クリックによるFocus喪失で確定。
5. Ctrl+Zで1、Ctrl+Yで2になることを確認。
6. Undoを2回行い、検証前のPosition X=0へ戻してからEditorを終了した。

起動ログは `artifacts/logs/phase7-input-editor-stdout.log`、標準エラーログは0 bytes。
Shortcut直後の描画更新が遅れる場合は次Frameを観察し、最終値で判定した。
最終の旧経路ガード追加後はCPU/ビルドを再確認し、上記実機操作の再実行は行っていない。

今回のマイルストーンは完了。補助OS Window内のInspector、物理キー/IMEを含む全入力経路、
他Component全項目、Backend/DPI別回帰は未検証。次は補助Windowの切り離し/Close復帰と、
移動先Contextでの編集確定/Clipboard/履歴操作を独立したマイルストーンとして検証する。

## 2026-11-02 補助Window検証: Drag開始座標のBlocker

承認範囲: OpenGL ReleaseのTab切り離し、補助WindowでのClipboard・編集確定・履歴操作、
Close復帰、Window別Statistics/CSVの照合。製品コード修正は追加承認の対象とする。

### ビルドと実操作

- `scripts/build-windows.ps1 -Configuration Release -SkipBootstrap` を実行した。
  制限内ではリンクで既知の `LNK1101: MSPDB140.DLL` が再発した。
  承認後の制限外再実行は終了コード0、コンパイラ/リンカ警告0で成功した。
  PDB設定や製品コードは変更していない。
- 通常の `Root/x64/Release/Root.exe --backend=opengl` を `Root/Root` から起動し、
  Raven UIのMenu/Tab/Panel本文とScene描画を確認した。ImGui fallbackは使用していない。
- Inspector TabとStatistics TabをそれぞれDock外のMenu高さまでDragしたが、
  OS Window一覧にはMain Windowしか現れなかった。
- Inspectorのログは `MouseMovedEvent: 1586, 46` の後に
  `MouseButtonPressedEvent: 0 at 1662, 13`、移動/Releaseも `1662, 13` となった。
  Statisticsでも開始側の移動 `587, 804` の後にDown/移動/Releaseが `747, 13` となった。
  Down時点で開始Tabの座標を失っており、Dock切り離し要求の成立を確認できなかった。
  これらはアプリのWindow client座標で、縮小された観察画像の座標とは区別する。
- Alt+F4による終了を起動ログの `WindowCloseEvent` で確認した。
  標準エラーログは空。検証前のDock Primary/Backupを
  `artifacts/phase7/auxiliary-backup/` へ退避した。

ログ: `artifacts/logs/windows-application-release-x64.log`（成功した再実行）、
`artifacts/logs/phase7-aux-editor-stdout.log` / `phase7-aux-editor-stderr.log`。

### 実装調査と追加修正候補

- `WindowsWindow.cpp` のMouse Button/Scroll callbackは配送時に `glfwGetCursorPos()` で
  現在位置を読み直す。開始側のCursor Moveが先に配送されてもDownは後の位置を読む経路があり、
  今回のログと整合する。低Frame Rateや入力注入時に蓄積したイベントの位置を保持する方針を
  検討する。発生条件全体や通常の物理Mouseでの再現性は未確定。
- `Application::OnAuxiliaryUIEvent()` は補助Contextの `RouteKeyEvent()` へ配送して終了する。
  Mainの `EditorLayer::OnEvent()` にある `TryHandleEditorHistoryShortcut()` への接続はない。
  補助WindowでのUndo/Redoを妨げる静的な不具合候補として扱い、実機ではまだ再現確認していない。
  修正する場合は、移動先ContextのText/Popup/Captureを尊重し、Editorの具体的責務をCoreへ
  持ち込まず共有Historyへ接続する必要がある。

### 判定

Release通常ビルドは合格。Tab切り離し以降の実機検証は入力座標のBlockerにより未完了。
補助Window内の編集/Clipboard/Undo/Redo、Close復帰、複数WindowのCSVは未検証であり、
合格やDock実装の不合格に置き換えない。新しい性能Baselineは採取していない。
製品コードを変更していないためCPU/GPUテストは再実行していない。
Phase 7の未完了チェックは維持する。

次の追加承認候補は、Mouse入力のイベント位置保持、補助ContextからのEditor履歴接続、
それらのCPU回帰とDebug/Releaseビルド、および今回中断したOpenGL Release実機検証とする。

## 2026-11-02 Mouseイベント座標と補助Context履歴配送の修正

上記二つの修正、CPU回帰、Debug/Release通常ビルド、OpenGL Release実操作への追加承認を受けて実施した。

### 変更と責務

- `WindowsWindow.cpp`: 既存Win32 bridgeがButton/Wheel messageのlParamから位置を取得する。
  Buttonは符号付きClient座標、Wheelは符号付きScreen座標をClientへ変換し、
  GLFW Window論理寸法へ合わせる。Framebuffer倍率やUIのUserScaleは掛けない。
  前のWndProcがGLFW callbackを呼ぶ間だけ位置を公開し、入れ子messageの終了時に元へ戻す。
  Bridge未導入時とFocus喪失等による合成Eventでは既存の現在位置取得を維持する。
- `Layer.h/.cpp`: 未処理UIキーだけを購読する `OnUIKeyEvent` を追加した。
  既定Layerは何も処理しない。補助Windowの通常 `OnEvent` をMain/Runtimeへ転送しない。
- `Application.h/.cpp`: `RouteUIKeyEvent` が入力元Contextを設定し、Widgetが処理してから
  未処理キーを前面Layer優先で一度ずつ配送する。Main/補助Windowの両方から同じ入口を使用する。
  Contextは配送中の非所有参照で、Callbackを保持しないため購読解除や新たな所有関係は不要。
- `EditorLayer.h/.cpp` と `EditorShortcutRouter.cpp`: 共有Historyの操作はEditor側に残す。
  Mainまたは実際にEditor Panelが移譲されたContextだけを対象とし、無関係なUI Windowでは処理しない。
  Text/Popup/Gizmoに加えMouse Capture/Drag中も履歴操作を拒否し、Repeat/Key Releaseを無視する。
  Mainの既存OnEvent内の履歴配送は新しい購読口へ移し、二重実行を防いだ。
- 新規ファイル、移動、削除はない。既存の空 `Layer.cpp` を使用しており、structure/filtersの構成変更はない。

### 自動検証

| 検証 | 終了コード | 結果・スクリプト報告の警告行数 |
| --- | --- | --- |
| Release CPU UI | 0 | 成功、64 |
| Debug CPU UI | 0 | 成功、84 |
| Release通常ビルド | 0 | 成功、0 |
| Debug通常ビルド | 0 | 成功、20 |

前回のPDB関連制限を踏まえ、承認を受けて制限外で実行した。プロセス内の `CL=/MP4` を使用。
既存C4244とDebugのLNK4098を含む警告行数であり、独立した警告件数とは扱わない。
追加部分のコンパイルで見つかった座標型include不足とテストのEvent namespace指定を修正し、
上表は修正後の最終コードで確認した結果。GPU統合、Allocation計測モードは今回再実行していない。

新規回帰はApplicationの実キー配送、Context移動後のClipboard/履歴、入力優先順位と単一消費を確認する。
Windows固有回帰はGPUを作らないNo-API Windowを生成し、実際のWndProc→GLFW→Core Eventで
queue済みDown/Move/Up、負のClient座標、縦横WheelのScreen→Client変換を確認する。
Windows固有回帰にはOS Windowが生成できる環境が必要。詳細は `Root/Tests/README.md` へ追記した。

ログ: `artifacts/logs/phase7-aux-fix-cpu-Debug.log` / `phase7-aux-fix-cpu-Release.log`、
`phase7-aux-fix-build-Debug.log` / `phase7-aux-fix-build-Release.log`。

### OpenGL Release実機とCSV

- 通常Game SceneでFloorを選択し、Inspector TabをDock外へDragした。
  ログはMove/Downがともに `1586, 46`、次のMove/Upがともに `1662, 13` となり、
  Downが後の現在位置へ置き換わる問題が解消した。`Raven - Inspector` OS Windowの生成を二回確認。
- 補助Windowは二回とも本文が黒いままだった。編集対象が見えないため補助Window内の
  Paste/確定/Undo/Redoの実操作は実施していない。CPU回帰の成功で実機合格に代用しない。
- Alt+F4で補助WindowだけをCloseすると、二回ともInspector TabとFloorの内容が元のDockへ戻った。
  二回目の復帰後もPosition X欄へのClick/Focus応答を確認した。
- Statisticsで開始→二回目の切り離し→補助WindowをClose→停止→Save CSVを実行した。
  開いている間はRendered Windows=2でMain ID=1と補助ID=3、Close後は1件へ戻った。
  補助Windowの表示は黒いが、CPU診断では339 commands / 1356 vertices / 2034 indicesを報告した。
  これはCPU描画要求の数であり、GPU描画成功や可視pixelの証拠にはしない。
- `artifacts/phase7/opengl-release-auxiliary.csv` へ複製したCSVは、Mainと全Windowが各1225 samples、
  補助Windowが796 samples、各Scopeに12指標、C++ Allocationは0 samples/N/A。
  lifetime peakを除く通常9指標で `total平均 × total samples = Σ(Window平均 × Window samples)` を
  許容誤差1e-6で確認し、不一致0。Close済みWindowの記録保持と省略Window samples=0も確認した。
  操作とビルド負荷を含む診断CSVであり、正式な性能Baselineではない。
- Editor終了後、検証前に退避したDock Primaryを復元した。Primary/Backup双方が
  退避時のSHA-256と一致することを確認した。

### 残るBlockerと未検証

二回目のClose復帰後、Main Inspectorで新しい配送経路の実機確認を始めたところ、
Position X欄へのClick後、次のキー操作前にEditor Windowが一覧から消えた。
最後の起動ログにはMouse Down/Upがあり、終了要求の `WindowCloseEvent` と標準エラーはない。
終了コードや例外情報は採取できておらず、Crashか正常終了条件の誤成立かは未確定。
Windows障害ログ照会でも、この終了に対応する記録は得られなかった。

関連する静的なLifetime懸念として、`WindowsInput` の生成ごとに `Input::s_Instance` が上書きされる一方、
補助Window破棄の `m_Input.reset()` 後に生存Windowへ戻す処理が見当たらない。
Applicationは毎Frameこの参照からEscape等をpollする。今回の終了との因果関係は未確定だが、
破棄済み入力参照を残さない設計を次の修正候補とする。今回の承認範囲では変更していない。

黒画面はDrawList以降のLayout/Clip/Renderer/Context状態とGPU資産を切り分ける必要がある。
今回の実装は座標保持とキー配送までとし、描画と入力provider寿命の追加変更は行っていない。
Phase 7のMulti-Viewport/安定性チェックは未完了を維持する。
次の追加承認候補は、補助Windowの本文表示と入力provider寿命の修正、Close反復の回帰、
補助Window内の編集/Clipboard/Undo/Redo実機検証、CPU/GPU検証とDebug/Releaseビルドとする。

## 2026-11-02 補助Window本文・入力provider寿命・移譲後Inspectorの修正

承認範囲: 補助Window描画と入力参照の寿命の修正、CPU/GPU回帰、Debug/Release通常ビルド、
実機の編集・Clipboard・履歴操作とClose反復。

### 原因と変更

- `UIContext.cpp`: Rootの確定寸法がViewportへ接続されていなかった。
  自然サイズに寄与しないPanelを持つStretch/Clip付きTab Pageを補助Rootへ移すと、
  0寸法の領域へ配置され、描画要求が存在しても可視領域を確保できない。
  BeginFrameでRootをWindow論理Viewportへ合わせ、寸法変更時だけ再設定する。
  既存Layout/Hit Testと同じWindow論理座標を使い、Framebuffer倍率やDIP倍率を二重に掛けない。
- `Input.h/.cpp`、`WindowsInput.cpp`: 各Window所有providerを非所有の連結で登録し、
  生成順で最初の生存providerを互換Global polling先とする（通常はMain）。
  補助Window生成では入力先を奪わず、任意順の破棄でも連結を外して生存参照へ更新する。
  最後の破棄後は既存Neutral fallbackへ戻る。追加の動的確保は行わない。
  コピー/移動を禁止し、Window生成/破棄と同じThreadで使用する契約を明記した。
  Window別UI Eventは引き続き入力元Contextへ配送し、Global pollingと混同しない。
- `OpenGLRHICommandList.cpp`: GPU回帰で既存のstate復元がGL_INVALID_ENUM（1280）を出すことを検出。
  OpenGL Core Profileでは無効なGL_FRONT/GL_BACK個別のPolygon Mode復元を、
  GL_FRONT_AND_BACKの一回の復元へ変更した。UIが一時的にFillへ変更しても元のLine modeへ戻ることを確認。
- `InspectorRavenPanel.h/.cpp`: 最初の再検証では本文とPreviewが表示されたがUndoが成立しなかった。
  生成時のMain Contextを保持してFocusを読むため、移譲後の入力を毎FrameのSnapshot同期が
  非編集中としてSetValueし、未確定入力とCommit基準値を上書きしていた。
  保存したContext参照を廃止し、Panel Rootの現在の所有ContextからFocusを取得する。
  Transform/Camera/RigidBody/Colliderの同期は同じ取得口を使い、Command/Entity寿命検証は維持する。
- 新規ファイル、移動、削除はない。structure.txt/filtersへ構成項目の追加は不要。

### 最終コードの自動検証

| 検証 | 終了コード | 結果・スクリプト報告の警告行数 |
| --- | --- | --- |
| Debug通常ビルド | 0 | 成功、0 |
| Release通常ビルド | 0 | 成功、0 |
| Debug CPU UI | 0 | 成功、64 |
| Release CPU UI | 0 | 成功、64 |
| Debug OpenGL GPU（Font Atlas必須） | 0 | 成功、0、SKIPなし |

既知のPDB/実行環境制限を避け、承認後の制限外実行を使用した。プロセス内の `CL=/MP4` を指定。
CPUの警告は既存C4244を含む行数であり、独立した警告件数ではない。
途中の再コンパイルにはDebugのLNK4098もあり、最終インクリメンタルの警告0は既存警告解消を意味しない。
Allocation計測モード、Release GPU統合、GitHub Actionsは今回再実行していない。

追加回帰:

- CPU: providerの中間/先頭/末尾破棄、Main終了時の生存provider選択、反復Close、最後の破棄後のNeutral値。
- CPU: 実InspectorをDockから別Contextへ移し、Resize時のRoot/Page/Panel寸法と非空Clip、元Paneへの復帰。
- CPU: 移譲後の不完全数値 `1e-` の保持、Snapshot更新を挟むPreview→Enterでの一回のCommand確定、
  移動先/復帰先のUndo/Redo、復帰後のFocus喪失確定。
- GPU: 共有OpenGL Contextを3回生成し、移譲したStretch/Clip付きTab本文の赤pixelをreadbackで確認。
  補助Window Resize後とMain Dockへ戻した後も採取し、Core Polygon Mode復元とGL errorなしを確認。
  default framebufferの検証なので小さいOS Windowを表示する。Font AtlasとTexture uploadの既存回帰も通過。
  初期のpixel不一致はWin32装飾付きWindowの実幅が要求64より広く、Window中央が64幅Dockの外だったため。
  本文内のWindow論理座標を実Framebuffer座標へ変換して採取するようテストを修正した。

ログ: `artifacts/logs/phase7-aux-display-build-Debug-final.log` / `phase7-aux-display-build-Release-final.log`、
`phase7-aux-display-cpu-Debug-final.log` / `phase7-aux-display-cpu-Release-final.log`、
`phase7-aux-display-gpu-Debug-final.log`。

### 最終OpenGL Release実機

通常Game Scene、RTX 3080、Raven UI、ImGui fallbackなし。
最終実機操作に使用したソースの後続変更は、GPUテストの無効寸法ガードと文書だけ。

1. Floorを選択しInspector TabをDock外へDrag。補助Windowで英語Caption、Transform、Collider本文を確認。
2. Position X=0を選択しASCII `1`を貼り付け、Frameを跨いだ入力保持とEnter確定を確認。
3. 補助Windowの背景をClickしてText Focusを外し、Ctrl+Zで0、Ctrl+Shift+Zで1へ戻ることを確認。
4. 補助WindowをAlt+F4でClose。元DockのInspectorが値1で復帰し、MainのCtrl+Zで0へ戻ることを確認。
5. 二回目の切り離しでは `2` を貼り付け、Enterせず入力中に補助WindowをClose。
   元Dockで値2が保持され、MainのCtrl+Zで0へ戻った。Close/Focus喪失で確定した編集をUndoできた。
6. 三回目は補助Inspectorを最大化し、Resize後も本文と値0が表示されることを確認してClose。
   MainでCtrl+Y→2、Ctrl+Z→0を確認し、三回のClose後も入力/履歴操作が継続することを確認。
7. 検証前のPosition X=0でMainをAlt+F4終了。監視したプロセス終了コード0、標準エラー0 bytes。

最終実機ログ: `artifacts/logs/phase7-aux-display-editor-final-stdout.log` / `phase7-aux-display-editor-final-stderr.log`、
`phase7-aux-display-editor-final-exit.log`。
最初の実機セッションも補助Windowを開いたままMainを閉じ、終了コード0を確認した
（`phase7-aux-display-editor-exit.log`）。このセッションはInspector Context修正前であり、
編集/履歴の合格結果は上記最終セッションのみを採用する。
Editor終了後にDock Primary/Backupを `artifacts/phase7/aux-display-backup/` から復元し、
両方のSHA-256一致を確認した。既存作業差分と既存CSVは保持した。今回の新規CSV採取は行っていない。

### 判定と残課題

今回のマイルストーンは完了。補助Windowの本文表示、入力参照寿命の回帰、実Inspectorの編集/Clipboard/
確定/Undo/Redo、3回のClose復帰とResize、正常終了を確認した。
前回の予期せぬ終了の例外/終了コードは採取できていないため、原因を断定した事後解析とは扱わない。
破棄済みprovider参照を残す経路を修正し、今回の反復では再発しなかったという範囲の結果とする。

別途、補助Windowを開いたままMain終了した後の起動ではInspector Tabが保存配置に含まれず、
View Menuで再表示が必要だった。MainのDock保存と補助Tabの復帰順序/保存対象を次の独立課題とする。
この配置保持は今回の描画/入力寿命修正に追加せず、検証前の配置へ戻している。

全Component全項目、物理キー/IME、複数MonitorのDPI変更、Scene切替、長時間の開発セッション、
Debug実機、DX12/VulkanのEditor操作と性能Baselineは未完了。
Phase 7全体のチェックは維持し、次は補助Windowを開いたまま終了する際の配置保存と、
Backend/DPI別回帰を別マイルストーンとして進める。

## 補助Windowを開いたまま終了するDock配置保存（2026-11-02）

承認された今回の範囲は、切り離しTabを終了時の保存配置へ含める対応。
Applicationが切り離し前のPane・Index・選択・順序を値で保持し、Editorの保存用Snapshotへ
逆順で合成する。生存中の補助RootとClose復帰待ちの所有Contentを対象とし、
Widget所有権、補助Windowの破棄順、GPU終了順は変更していない。
JSON Version 1を維持し、再起動時はMain Dockへ戻す。

### 差分とCPU回帰

- `Application.h/.cpp`: 保存可能な切り離しTab情報の収集。raw pointerは逆参照せず所有Rootと照合。
- `UIDockSpace.h`: Pointerを持たない復元情報の型。
- `EditorDockLayout.h/.cpp`: 保存用Snapshot合成。現在のTab・選択を優先し、重複登録を防止。
  元Pane消失時は生存する既定Pane、次にTree先頭Paneへ復元。不正入力時は出力を更新しない。
- `EditorLayer.cpp`: 合成成功時だけ保存し、失敗時は診断を出して既存ファイルを維持。
- `UITextReflowTests.cpp`: 同じPaneの複数Tabの順序/選択、非表示維持、重複通知、Fallback、
  保存/読込/再生成、無効入力、および実行中DockとContent所有権の不変性を検証。
- Dock契約文書、Tests README、ロードマップを更新。Raven以下のファイル構成変更はない。

Debug/Release x64 CPU回帰はどちらも成功。通常アプリケーションのDebug/Release x64ビルドも成功。
通常Debugは既存のコンパイラ/リンカWarning 2件、Releaseは0件。CPUビルドの既存Warningも残る。
ログは `artifacts/logs/phase7-dock-save-cpu-Debug.log`、`phase7-dock-save-cpu-Release-final.log`、
`phase7-dock-save-build-Debug.log`、`phase7-dock-save-build-Release.log`。

### OpenGL Release実機

1. Animation DebugをView Menuで非表示にし、Game Viewを選択。
2. Scene View、Game View、Inspectorを順に切り離し、Mainと補助3 Windowの存在を確認。
3. 補助を閉じずMainをAlt+F4終了。終了コード0、標準エラー0 bytes。
4. 保存JSONでScene/Gameの順序1001→1002、選択1002、Inspector1004を確認。
   Tabは1003/1001/1002/1005/1004が各一つで、非表示のAnimation Debug1006は含まれない。
5. 再起動でMainのみ生成され、Scene/Game、選択中Game、Inspector、Statisticsを目視確認。
   Animation Debugは非表示のまま。再度終了コード0、標準エラー0 bytes。
   2回目の保存と最初の保存のSHA-256が一致し、往復後も配置が安定していることを確認。

実機ログは `artifacts/logs/phase7-dock-save-editor-*` と `phase7-dock-save-restart-*`。
保存結果は `artifacts/phase7/dock-save-shutdown.layout.json`。
終了後、検証前のPrimary/Backupを `artifacts/phase7/dock-save-backup/` から復元し、
両方のSHA-256一致を確認した。

今回のマイルストーンは完了。Main/補助Closeが同一PollEventsに届く経路は静的確認のみ。
Debug実機、DX12/Vulkan、複数Monitor/DPI、Scene切替、長時間操作は未検証で、Phase 7全体は未完了。
次はBackend/DPI別回帰を独立したマイルストーンとして進める。

## DX12 Editor回帰の初期調査（2026-11-02、未完了）

承認範囲はDX12 Editorの基本操作と補助Window回帰、および発見した不具合の修正。
既存Release実行ファイルを `--backend=dx12` で起動し、MainのScene View、文字、
Hierarchy、Inspector、Animation Debugの表示を確認した。
Floor選択後、Position Xを物理キーで0→1へ変更してEnter確定し、Text Focusを外して
Ctrl+Zで0、Ctrl+Shift+Zで1、再度Ctrl+Zで0に戻ることを確認した。
Computer Useの`type_text`では値変更を確認できず、合格結果には含めない。

Inspector TabをDock外へDragしても補助Windowは生成されず、MainのInspectorは保持された。
静的調査では `Application::CreateUIWindow` がOpenGL以外を拒否し、
`WindowManager::RenderWindow`もOpenGL専用であることを確認した。
DX12SceneRuntimeは単一WindowのContextとDeviceを所有し、DX12SceneContextは
Queue/SwapChainを所有する。DX12補助Window対応には、この責務を整理し、
MainのViewport Textureを利用できる同一Device上の補助SwapChain、Frame同期、
Resize/Close時のGPU待機と参照解放を実装する必要がある。
OpenGL制限だけを解除する変更や、別DeviceのRuntimeを単純複製する変更は行っていない。

Main終了コード0、標準エラー0 bytes。ログは `artifacts/logs/phase7-dx12-editor-*`。
検証前のDock Primary/Backupを `artifacts/phase7/dx12-backup/` から戻し、双方SHA-256一致。
今回はC++変更なし、ビルド/CPU回帰の再実行なし。Resize、再起動、Debug実機、
補助Windowの描画/編集/復帰、DX12性能Baselineは未検証。
マイルストーンは未完了とし、GPU基盤の設計拡張は範囲の再承認後に着手する。

## DX12補助Window実装・回帰（2026-11-02）

上記の初期調査後、Device/Queue共有と補助SwapChainを含む設計拡張の承認を受けて実装した。
今回のマイルストーンは完了。Phase 7全体の未完了チェックは維持する。

### 構造と差分

- `DX12SceneContext.h/.cpp`: Factory/Adapter/Device/Queueを共有所有する。
  SwapChain、CommandList/Allocator、Fence、Frame保持ResourceはWindow別に所有し、
  各ContextのShutdownでQueueの完了を待ってから解放する。最後のContext解放まで共有Deviceは生存する。
- `IExplicitSceneRuntime.h/.cpp`: 補助UI Windowの生成・描画・破棄境界を追加。
  未対応Backendの既定実装は拒否する。
- `DX12SceneRuntime.h/.cpp`: Mainと同じDeviceで補助UIを準備し、同じQueueへ
  Main描画後に投入する。MainのViewport TextureとUI Pipelineを共有し、
  Window別SwapChainのResizeとFence待機、Close時の解放を担う。
- `Application.h/.cpp`: DX12補助Windowを既存のWindow別UIContext・Clipboard・入力・
  Tab復帰/保存に接続。描画失敗時は再利用せずCloseを予約する。
  WindowManagerの既存CloseCleanupを使うため同クラスの変更は不要。
- `UIFontTextureGPUIntegrationTests.cpp`: `--dx12-auxiliary`でDX12専用のGPU回帰を追加。
  Tests READMEとロードマップを更新。新規ソースファイルなし、structure/Filter更新対象なし。

実機のMain最大化では、通常ApplicationがDX12のSwapChainへ寸法変更を伝えず、
旧BackBufferへ新しいViewport寸法で描画してUIが拡大/欠落する既存問題を発見した。
DX12はAcquireでResizeRequiredを返さないため、ApplicationがFrame開始前に
Framebuffer実Pixel寸法を比較してRuntime::Resizeへ通知するよう修正した。
0寸法/最小化中は描画を待ち、復帰待ち時間をSceneのdtへ加算しない。
この待機中は補助UIも更新を停止する。Main最小化中に補助だけを独立更新する対応は含まない。

### 自動検証

- Debug/Release x64通常ビルド成功。最終Debugは既存Warning 2件、Releaseは0件。
- Resize修正後のDebug/Release CPU UI回帰成功。
- Debug/Release DX12 GPU回帰成功。共有Device同一性、Main Context終了後の補助描画、
  3回の補助登録/描画/Resize/解放、重複拒否、Close後の拒否、Shutdownを検証。
  Debug GPUログのWarning/Error/Corruption/FAIL検索は0件。
  DX12のpixel readback検証は含まず、表示の確認は下記実Editorで実施。
- 既存OpenGL Release GPU回帰（Arial Font Atlas、Texture診断、共有Contextの
  pixel readbackと3回のClose/復帰）成功。

ログ: `artifacts/logs/phase7-dx12-aux-build-{Debug,Release}-final.log`、
`phase7-dx12-aux-cpu-{Debug,Release}-final.log`、`phase7-dx12-aux-gpu-{Debug,Release}.log`、
`phase7-dx12-aux-opengl-regression.log`。GPU回帰後の変更はMain Resize処理とコメント/文書のみ。

### Release実Editor

Computer Use skillの実機操作で次を確認した。

1. Floor選択後にInspectorを切り離し、本文と数値を確認。
2. 補助WindowでPosition Xを物理キーで0→1、Enter確定、背景Click後に
   Ctrl+Zで0、Ctrl+Shift+Zで1、再度Ctrl+Zで0を確認。
3. 補助Inspectorを最大化し本文表示を確認。Close後にMainのInspectorへ値0で復帰。
4. Scene ViewとGame Viewを切り離し、共有Viewport Textureの表示を確認。
   Inspectorも再度切り離し、Mainと補助3 Windowの存在を確認。
5. 補助3 Windowを開いたままMain終了。終了コード0、標準エラー0 bytes。
   保存JSONに全6 Tabが各一つ含まれることを確認。
6. 再起動ではMainのみ生成され、Scene/Game/Inspector/診断PanelがDockへ復帰した。
   このセッションの最大化で旧SwapChain寸法の問題を検出。終了コード0、標準エラー0 bytes。
7. Main Resize修正後のReleaseで再実行し、最大化後もHierarchy、Scene/Game、Inspector、
   下部診断Panelが全て正しい領域に配置されることを確認。終了コード0、標準エラー0 bytes。
   通常サイズへの復帰操作はComputer Useの高解像度Window操作が安定せず、合格結果に含めない。

実機ログ: `phase7-dx12-aux-editor-*`、`phase7-dx12-aux-restart-*`、`phase7-dx12-aux-final-*`。
保存結果: `artifacts/phase7/dx12-aux-shutdown.layout.json`。
終了後、検証前のPrimary/Backupを `artifacts/phase7/dx12-aux-backup/` から復元し、両方SHA-256一致。

未検証: Debug実Editor、全Component、Main最小化/復帰の実機反復、異DPI Monitor、
Scene切替、長時間負荷、Device Removed等の障害注入、DX12性能Baseline、Vulkan補助Window。
次の候補はVulkan補助Windowの対応、またはDPI別回帰を独立マイルストーンとして進めること。
