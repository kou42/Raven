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
