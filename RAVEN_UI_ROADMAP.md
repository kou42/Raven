# Raven UI 実装ロードマップ

最終更新: 2026-09-23  
対象: `Root/Root/Raven/UI` / Raven Editor  
状態: Phase 1 実装中（InputText / InputNumberの操作確認済み、Font/IME全項目は未完了）。Phase 10のDPI対応・Font Atlas診断はPR #253〜#269で実装し、ユーザーの実環境でGPU統合テスト・UI回帰テスト・通常起動の問題なしを確認済み。OS Window基盤はPR #247で実装・動作確認済み。Phase 7 Drag & DropはPR #249で実装・ユーザー動作確認済み。Phase 8 Tab SystemはPR #250で実装・ユーザー動作確認済み。独自UIの論理Window移動・Resize・OpenGL Multi-Viewportドラッグ分離/復帰はPR #271で実装し、ユーザーの実環境で動作問題なしを確認済み。Phase 11 Immediate Mode基盤・Physics DebugパネルはPR #272で実装し、ユーザー動作確認済み。異DPI・他Backend等は継続課題

## 目的と原則

Raven独自のRetained Mode UI Treeを維持し、Dear ImGui相当のEditor操作性を段階的に獲得する。既存Dear ImGui Editorは独自UIへの移行が検証できるまで維持する。

- UIElement / UIContextは入力・レイアウト・UIの寿命を管理する。
- UIDrawListはフレーム単位の描画要求を保持する。GPU API固有値を入れない。
- UIRendererは描画API境界を担う。RHI移行中の既存経路を壊さない。
- 各Phaseは小さなPRに分割し、実装・テスト・ビルド結果を記録する。
- 「実装済み」はコード確認、「検証済み」は実際のテストまたはビルド確認を意味する。計画を完了扱いにしない。

## 現状の基盤（UI: 2026-09-22、OS Window: 2026-09-22 コード・動作確認）

- UIContext: BeginFrame / EndFrame、Mouse Routing、Capture、Keyboard Focus、Tab移動。Drag & DropのPayload、移動閾値、Drop先探索、Preview、Cancel、時間駆動Tickに対応（PR #249）。
- UIElement: Retained Tree、Measure / Arrange、Absolute / Vertical / Horizontal、Transform、Clip、Opacity。
- UIDrawList: SolidRect / SolidCircle / SolidPolygon / Image。Text Commandは未実装。
- UIRenderer: API非依存の境界。OpenGLUIRendererは既存実装。
- Dear ImGui: EditorLayerでDockSpaceを利用。独自UI側のDockingとは別。
- OS Window基盤: PR #247をmasterへマージ。WindowManager、Window状態/Event、OpenGL Context共有・切替、補助Window別VAO、Framebuffer実Pixelサイズ、終了時Cleanupを実装し、ユーザーの実環境で動作確認済み。独自UIのWindow配置・入力配送・Multi-Viewport描画への接続は別途必要。

## 順序と進捗

| Phase | テーマ | 状態 | 完了条件 |
| --- | --- | --- | --- |
| 1 | Font Atlas / Text Rendering | 実装中 | Font読込、Glyph Atlas、DrawList経由の文字表示、日本語fallback、基本描画検証 |
| 2 | Text Layout / Text Measurement | 基盤実装済み・回帰テスト棚卸し済み（数値テスト・実描画検証は継続） | Measure/Arrangeと文字サイズ連携、改行・配置・Clip検証 |
| 3 | InputText / InputNumber | 実装中（InputText / InputNumberの基本操作・横スクロール確認済み、IME未対応） | Cursor、選択、編集、Clipboard、Undo/Redo、IME方針と検証 |
| 4 | ComboBox / Popup / Tooltip | 未着手 | Focus、閉じる条件、重なり順、入力伝播の検証 |
| 5 | TreeView / Table | 実装中（TreeView基礎・Demo・縦Scroll/Scrollbar・Drag & Drop・回帰テスト追加。Drag & Dropはユーザー動作確認済み／Table基礎・Demo・回帰テスト追加） | Hierarchy相当の選択・展開、表の列・Scroll・基本操作 |
| 6 | Window System | OS Window基盤（PR #247）と論理UIWindowの移動・Resize・前面化・Viewport移譲（PR #271）を実装し、ユーザー動作確認済み。Keyboard Focus等の拡張は継続 | 論理UI Windowの移動・Resize・Focus・Z順 |
| 7 | Drag & Drop | 実装・ユーザー動作確認済み（PR #249。TreeView同一/別View移動、Root末尾Drop、自動Scroll/展開、無変更Drop抑制を含む） | Payload、Capture、Drop target、Cancel |
| 8 | Tab System | 実装・ユーザー動作確認済み（PR #250。選択・追加・削除・移動、Content切替、Overflow・省略表示、Demo・回帰テスト追加） | Tab選択・追加・削除・移動 |
| 9 | Docking System | 実装・ユーザー動作確認済み（PR #251。Split / Pane間Tab移動 / Dock Preview / Layout保存復元）。RavenUITest単体実行・GitHub Actions結果は未確認 | Split / Tab / Dock preview / Layout保存復元 |
| 10 | Theme / Style | 共通Style・状態別外観とDPI基盤・DIPレイアウト・DPI Font Atlas更新・失敗診断を実装、ユーザー動作確認済み（PR #252〜#269）。Window別の実DPI移動検証は継続 | 共通Style、状態別外観、DPI |
| 11 | Immediate Mode風API | 安定ID・Retained Widget再利用、基本Widget、Physics Debugパネルを実装・ユーザー動作確認済み（PR #272）。API拡張・Editor本格移行は継続 | 安定IDとRetained Element再利用、Debug UI検証 |
| 12 | Multi-Viewport | OS Window別UIContext・入力/IME配送・DPI/Framebuffer描画・Dock Tab切り離し/Close復帰を実装、ユーザー動作確認済み（PR #270、OpenGL限定）。論理Windowの自動ドラッグ分離・Mainへの再統合はPR #271で実装・ユーザー動作確認済み。異DPI・他Backendは継続課題 | OS WindowごとのUIContext / 入力配送、UI描画Target、生成・破棄・DPI・Focusの検証 |
| 13 | RHI Batching最適化 | 未着手 | Draw順・Clipを維持したBatchingと計測 |
| 14 | UI Debugger / Profiler | 未着手 | Tree / Focus / Draw Command / CPU・GPU指標表示 |

## Phase 2: Text Layout / Text Measurement 検証状況（2026-09-23）

- [x] `UITextLayout` の改行・Character/ASCII Word Wrap・左右中央配置、`UITextMeasurement` の共通Layout利用、`UILabel` の幅依存Measure / Draw接続をコード確認。
- [x] `Root/Tests/UITextReflowTests.cpp` の既存テストを棚卸し。`WrappingElement` の幅依存再MeasureとDPI関連テストは存在するが、実`UILabel`と固定Glyph MetricsによるLayout数値期待値テストは未確認。
- [ ] Layout / Measurementの数値回帰テスト、実UILabel / UIElement統合テスト、Glyph Clip / Alignment / Wrapの組合せテストを追加・実行する。
- [ ] Text DemoのResize・改行・配置・Clip・DPIを実描画確認し、結果を記録する。今回の棚卸しではビルド・テスト実行・GPU描画は未実施。
- 詳細な確認範囲、テストマトリクス、完了条件は [`RAVEN_UI_PHASE2_TEST_AUDIT.md`](RAVEN_UI_PHASE2_TEST_AUDIT.md) を参照。

## Window System / Multi-Viewportへの接続状況

- **実装・動作確認済み（PR #247）**: OS Windowの生成・状態・Event、WindowManagerによる複数Window管理、OpenGL共有Context、Window単位のFrame Lifecycle、補助Windowのdefault framebuffer描画とFramebuffer実Pixelサイズ対応、Context固有VAOの再構築・キャッシュ、Close CleanupとApplication終了時の補助Window解放。
- **Phase 6で実装・ユーザー動作確認済み（PR #271）**: UIContext内の論理UIWindow、タイトルバー移動・右下Resize・前面化、Mouse Capture/Cancel。Keyboard Focus等の拡張は継続課題。
- **Phase 12で実装・ユーザー動作確認済み（PR #270）**: OpenGL補助WindowごとのUIContext / UIRenderer、Window別DPI・Framebuffer実Pixel Viewport/Scissor、独立入力/IME配送、Root Widget移譲、Dock Tabの明示的・Frame境界予約切り離し、Close時の元Dock Pane復帰と復帰先消失時Main Root退避。CPU側回帰テストには複数Window逆順CloseとDock削除時の復帰シミュレーションを追加。
- **Phase 12のPR #271実装・ユーザー動作確認済み**: Mainの論理Windowをドラッグで補助OS Windowへ分離、Main領域へのドロップで再統合、Focus喪失とMouse Up取りこぼし補完、空補助Windowの遅延Close。
- **Phase 12の残課題**: 別DPIモニター間移動の個別検証、Vulkan/DX12描画Target対応、Window Close経路そのものの自動統合テスト。
- **次の接続作業**: 異DPIモニター間移動と自動Window Closeの統合テスト、論理WindowのKeyboard Focus/タイトル描画等を段階的に拡張する。既存Dear ImGui Editorは移行検証まで維持する。

## Phase 7: Drag & Drop 実装・検証記録（PR #249）

- [x] UIEventにPayloadとBegin / Over / Enter / Leave / Drop / End / Cancelを追加し、UIElementへDragイベント受信口を設ける。
- [x] UIContextで移動閾値、Captureとは独立したDrop先探索、クリック抑制、Escape / Subtree離脱によるCancel、Drag Preview描画を実装する。
- [x] EndFrameの時間駆動Tickと明示的なTickDragにより、ポインター停止中もDrop先を再判定する。
- [x] TreeViewの同一View・別View間Subtree移動、循環/ID重複防止、空白へのRoot末尾Drop、選択状態と通知を実装する。
- [x] TreeViewの端での自動Scroll、折りたたみNodeのHover遅延展開、Drop挿入線の境界補正、無変更Drop抑制を実装する。
- [x] RavenUITestの回帰テストを追加し、ユーザーから実環境で動作問題なしとの報告を受ける。
- [ ] 他WidgetへのDrop拡張、Scene Hierarchy等の実データモデル/Undo連携、追加の負荷・再入テストは別途検討する。Phase 7の基本完了条件とは分けて扱う。

## Phase 8: Tab System 実装・検証記録（PR #250）

- [x] UI非依存のUITabModelで安定ID、追加・選択・Close・強制削除・並び替え、通知を実装する。
- [x] UITabBarで選択・Hover・Close領域の描画とMouse操作、UIContext Drag & Dropによる並び替えを実装する。
- [x] UITabViewで選択TabのContent表示、削除時のContent破棄、通知の同期を実装する。
- [x] Headerの横スクロール、選択Tabの自動表示、UTF-8境界とFont Metricsを考慮したタイトル省略を実装する。
- [x] UITextDemoLayerへ5 Tabの操作パネル、RavenUITestへModel/View/Scroll/Dragの回帰テストを追加する。
- [x] ユーザーから実環境で動作問題なしとの報告を受ける。
- [x] Dockingとの接続、Tabの別Paneへの移動、Layout保存復元はPhase 9 / PR #251で実装しユーザー動作確認済み。負荷・再入テストは継続課題。

## Phase 9: Docking System 実装記録

- [x] UIElementから独立したUIDockLayout / UIDockNodeを追加。Tabs Leaf / 二分Split、安定ID、親参照、分割比率、既存Leafの所有権移動を実装。
- [x] RavenUITestに入れ子Split、Tab選択維持、不正比率・非Leaf・未知ID拒否の回帰テストを追加。
- [x] ユーザーからPhase 9の動作チェックで問題なしとの報告を受ける。Windowsビルドのログ・RavenUITest単体実行結果・GitHub Actions結果はこのチャットでは未取得。
- [x] UIDockGeometryで入れ子Splitの矩形配置、Splitter矩形、最小Pane幅、Drag差分からの比率更新を実装。狭いViewportでは負寸法を防ぐ。回帰テストを追加。
- [x] UIDockSpaceでLeafにPaneを対応付け、SplitにUISplitterを生成。マウスDrag差分をUIDockGeometry::Resizeへ接続し、配置を更新。回帰テスト追加。
- [x] UIDockSpaceにUITabView生成・Tab追加/選択/Close/並び替えの同期経路を追加。回帰テスト追加。
- [x] UITextDemoLayerに左右・上下Split、3 Pane、Tab切替を確認するDocking Demoを追加。OnDetachでRootから安全に削除。
- [x] ユーザーからDocking Demoの実環境動作問題なしとの報告を受ける。
- [x] UITabView::ExtractTabでContentを破棄せず移動、UIDockSpaceで別Paneへ移動しDrop Previewを表示。Content同一性の回帰テスト追加。
- [x] 空Tab Leafの削除、親Splitの解消、Siblingの昇格、不要Widgetの解放を追加。入れ子Treeの回帰テスト追加。
- [x] Dock TreeのPreorder幾何Snapshotを保存・検証付き復元。空DockSpaceへの復元とSplitter再生成、回帰テストを追加。
- [x] DockSpace SnapshotにTab順序・タイトル・Closable・選択状態を追加。FactoryでContentを再生成し、事前検証失敗時は元Treeを保持。
- [x] Raven CoreのJsonParser/Writerを再利用したSnapshot JSON/ファイル入出力、version検証、64bit ID文字列保存、破損JSONの無変更保証。
- [x] Text Demoで起動時にJSON復元、終了時にJSON保存。RAVEN_UI_DOCK_LAYOUT環境変数で保存先指定、初回/不正ファイルは3 Pane構成にフォールバック。
- [x] 一時ファイルへ全量書込後に旧版を.bakへ退避し置換。置換失敗時の旧版復帰、主ファイル欠落時のBackup読込、8 MiB読込上限とファイル回帰テスト。
- [x] RestoreSnapshotのWidget生成・Tab追加・選択失敗時に追加Paneを破棄し旧Treeをmove復帰。ID発行状態も保持。Factory失敗時の既存Split不変テスト。
- [ ] RavenUITest単体実行結果とGitHub Actionsの確認、再起動後のSplit/Tab状態の個別検証記録、正式Editorへの導入、異常なUIElement追加失敗の注入テスト、負荷・再入テスト。

## Phase 10: Theme / Style 実装・検証記録（PR #252）

- [x] UIThemeを追加し、UIContext単位でThemeを値保持。Default Darkは従来の配色を維持し、Default Lightを定義。
- [x] Button / Panel / Slider / InputText / ScrollBar / Label / TabBar / TreeView / Tableの共通Style・状態別配色を追加。
- [x] 各Widgetは描画時にContextのThemeを解決し、Retained TreeやTab Model、Table DataSourceの再構築なしで配色を切り替える。
- [x] 既存の個別色SetterをThemeより優先し、Context非所属のWidgetでは従来配色を維持する。
- [x] Theme切替・Context分離・DrawList配色・個別指定優先の回帰テストをRavenUITestへ追加。
- [x] ユーザーから実環境での動作チェックに問題なしとの報告を受ける。個別のテスト実行ログとGitHub Actions結果はこのチャットでは未取得。
- [x] PR #253〜#259でDPI取得・UIContext同期、DIP座標・レイアウト・文字描画を実装。PR #260〜#263でDPI別Font Atlasキャッシュと描画前の一括更新を実装。
- [x] PR #264〜#268でFont Atlas失敗診断を段階追加。RHI Device未初期化、Texture Resource生成失敗、GPU pixel転送失敗を区別。
- [x] PR #269でOpenGL実ContextのTexture転送・GPU readback・不正サイズ・既存GL error・任意の実Font Atlas生成を検証する独立テストを追加。ユーザーからGPU統合テスト、既存UI回帰テスト、通常Raven起動に問題なしとの報告を受ける（個別ログはこのチャットでは未取得）。
- [ ] OS Windowを別DPIのMonitor間で移動した際のFont・入力座標・Clip・Multi-Viewport連携を検証する。Phase 10全体の完了とは扱わない。

## Phase 1: 実装分割

1. [x] 既存TextureAsset / Texture生成API、Shader、OpenGLUIRenderer、Visual Studio project設定を確認。独立管理のinclude/stb_truetype.hを翻訳単位内に限定して使用し、AtlasをTextureAssetで所有する方針を決定。
2. [x] Glyph Metrics / Font Atlasの最小Runtime構造を実装する。Font読込・RasterizeはBuilderで実装、Texture再生成はAtlasの再Buildで対応（実行未検証）。
3. [x] UTF-8 decode、代替Glyph、ASCII・日本語コードポイントのGlyph取得経路を実装する。不正UTF-8と未収録文字の実行テストは未実施。
4. [x] DrawListへGlyph Quadの入口を追加し、既存Image Commandを再利用する方針を決定する。Font Atlasからの呼び出しは実装済み・描画検証は未実施。
5. [ ] RGBA8 Atlasを既存Image経路へ接続済み（BuilderからのGPU実行は未検証）。矩形/画像描画とScissor/Transformを回帰確認する。
6. [ ] UILabelとUITextDemoLayerを追加しDebug起動に接続済み。ASCII・日本語・複数行・Resize・高DPIの実描画確認は未実施。

### Phase 1で注意する点

- Font Rasterizerは独立管理のinclude/stb_truetype.h（upstream nothings/stb v1.26、public domain / MIT）を使用する。ImGui vendorへの新規依存は作らない。
- AtlasのTextureはDrawListの描画完了まで有効にする。GPU Texture IDをUI Coreに持ち込まない。
- 現在のOpenGLUIRendererはメインWindowのdefault framebufferを描画先にしている。別RenderTarget対応は別Phaseで整理する。
- Text表示が成功しても、Text Measurement / InputText完成とは扱わない。

## 作業記録

| 日付 | ブランチ / PR | 変更 | 検証 | 次の作業 |
| --- | --- | --- | --- | --- |
| 2026-09-21 | feature/raven-ui-roadmap | ロードマップとPhase 1の分割・完了条件を作成 | 文書のみ | Phase 1-1の依存関係確認、Font Atlas設計 |
| 2026-09-21 | feature/raven-ui-roadmap | UIDrawList::AddGlyphを追加。TextureAssetとImage Commandを再利用 | コードレビューのみ。ビルド・実行未検証 | Font Atlas / Glyph Metrics、Font読込とUTF-8対応 |
| 2026-09-21 | feature/raven-ui-roadmap | UIFontAtlasにGlyph Metrics登録・Atlas範囲検証・UV変換・DrawList接続を追加 | ビルド・実行未検証 | Font Loader、Rasterizer、Texture生成、UTF-8 |
| 2026-09-21 | feature/raven-ui-roadmap | UIUtf8とAppendTextを追加。代替Glyph・改行・Advanceに対応 | ビルド・実行未検証 | Font Loader、Rasterizer、Texture生成、文字列描画検証 |
| 2026-09-21 | feature/raven-ui-roadmap | stb_truetypeで指定文字集合をRasterizeしRGBA8 TextureAssetを生成するBuilderを追加。AppendTextの改行リテラルも修正 | ビルド・実行未検証。Fontファイル・GPU Contextが必要 | UILabelまたはText検証UI、実描画・日本語フォント・Atlas容量検証 |
| 2026-09-21 | feature/raven-ui-roadmap | upstream stb_truetype v1.26をincludeへ独立配置、BuilderのImGui依存を解消。UILabelを追加 | GitHub差分確認のみ。ビルド・GPU実描画未検証 | DemoへのFontロード・UILabel組込み、文字表示検証 |
| 2026-09-21 | feature/raven-ui-roadmap | UITextDemoLayerをDebug起動へ接続。RAVEN_UI_DEMO_FONTとOS標準候補からFontを選び、ASCII・日本語・改行・fallbackの文字列を表示する構成を追加 | GitHub差分確認のみ。ビルド・GPU実描画未検証 | Windows Debugビルド、文字表示・Font欠落・Atlas容量・Clip検証 |

| 2026-09-21 | feature/ui-input-text | UITextEditBufferを追加。codepoint境界のCursor/選択、挿入、Backspace/Deleteを実装 | GitHub上のコード確認のみ。ビルド・単体テスト未実施 | Character Event経路、InputText WidgetとCursor描画、InputNumber |\n\n| 2026-09-21 | feature/ui-input-text | GLFW char callback → Core Event → UIContext → InputTextを接続。文字表示、Caret、選択表示、左右/Home/End/Backspace/Delete、Click位置移動を追加 | GitHub差分確認のみ。ビルド・実描画未検証 | Demo接続、Mouse Drag選択、Clipboard、Undo/Redo、IME、InputNumber |\n\n| 2026-09-21 | feature/ui-input-text | UITextDemoLayerにInputTextを配置し、Mouse Captureによるドラッグ選択とCancelを追加 | GitHub差分確認のみ。ビルド・操作検証未実施 | Clipboard、Undo/Redo、IME、InputNumber、Cursorスクロール |\n\n| 2026-09-21 | feature/ui-input-text | 選択文字列の取得、最大128件のUndo履歴、Redo、Ctrl/Cmd+A/C/X/V/Z/Y、Shift+Ctrl/Cmd+Zを実装。DemoへGLFW Clipboard Adapterを注入 | GitHub差分確認のみ。ビルド・操作検証未実施 | Undo履歴結合、IME、InputNumber、横スクロール、Grapheme対応 |\n\n| 2026-09-21 | feature/ui-input-text | UIInputNumberをInputTextの子Widgetとして追加。有限数の完全一致解析、Min/Max、値変更通知、Demoを実装 | ビルド・実操作未検証 | 数値入力の無効文字制限、確定時の表示正規化、Step、横スクロール、IME |\n\n| 2026-09-21 | feature/ui-input-text | InputText入力候補フィルタ・Enter通知、InputNumber十進/指数表記の途中入力許可・確定正規化・Step増減を追加 | ビルド・実操作未検証 | Focus Lost確定、Step操作UI、横スクロール、IME、回帰テスト |\n\n| 2026-09-21 | feature/ui-input-text | UIElementのFocus遷移通知、InputNumberのFocus Lost/Window Focus Lost確定、上下キーによるStep増減を追加 | ビルド・実操作未検証 | テスト追加、数値Stepボタン、横スクロール、IME |\n\n| 2026-09-21 | feature/ui-input-text | 既存RavenUITestの回帰テストへUTF-8選択・Undo/Redo・履歴破棄、InputNumberの範囲・確定・Step・通知を追加 | テストコード追加済み、ビルド・実行未検証 | Focus/Clipboardのイベント経路テスト、実機ビルド、横スクロール、IME |\n\n| 2026-09-21 | feature/ui-input-text | UIContext経由のInputNumber文字入力・無効入力拒否・Clipboard・Undo/Redo・Focus移動/ClearFocus・上下Stepの回帰テストを追加 | テストコード追加済み、Windowsビルド・実行未検証 | 実行確認、Mouse経由のFocus解除、横スクロール、IME |\n\n| 2026-09-21 | feature/ui-input-text | InputText横スクロール・外側クリック確定・既存入力回帰テスト | ユーザーのビルド・動作確認で問題なし | ドラッグ中の領域外スクロール、IME |\n\n| 2026-09-21 | feature/ui-input-text | Capture中の領域外ドラッグ選択に横スクロール追従を追加 | GitHub差分確認のみ、今回の変更は動作未検証 | ドラッグ選択の左右端・短い文字列・Transform・IME |\n\n| 2026-09-21 | feature/ui-input-text | InputTextのHitCursorをUTF-8/Glyph Advanceの単一走査に変更。長文のマウス位置判定で繰り返しCursorXを呼ぶO(n²)処理を解消 | GitHub差分確認のみ、今回の変更は動作未検証 | 日本語混在・Fallback Glyph・長文のクリック/ドラッグ・IME設計 |\n\n| 2026-09-22 | feature/ui-tree-view | UITreeViewの階層ノード、単一選択、展開/折りたたみ、Mouse/Keyboard操作、Font描画の基礎を追加 | GitHub上のコード確認のみ。ビルド・実描画・操作テスト未実施 | Demo接続、回帰テスト、Scroll、Table |\n\n| 2026-09-22 | feature/ui-tree-view / PR #246 | UITextDemoLayerにScene/Player/Environmentの階層サンプルを配置。選択・展開ログ、OnDetach時の除去を追加 | GitHub差分確認のみ。Windowsビルド・描画・マウス/キー操作は未検証 | TreeView回帰テスト、Scrollと選択項目の可視化、Table基礎 |\n\n| 2026-09-22 | feature/ui-tree-view / PR #246 | TreeViewへWheel縦Scroll、選択行の自動可視化、Viewport Clip、スクロール検証用Demoノードを追加 | GitHub差分確認のみ。Windowsビルド・実描画・操作テスト未実施 | TreeView回帰テスト、Scrollbar、Table基礎 |\n\n| 2026-09-22 | feature/ui-tree-view / PR #246 | 折りたたみで選択ノードが隠れる場合に親へ選択移動。既存RavenUITestへTreeViewのID重複・所有関係・展開・選択・Scroll Clamp・Keyboard/Mouse/Wheel回帰テストを追加 | テストコード追加済み、Windowsビルド・テスト実行未検証 | 実機ビルド・回帰テスト実行、Scrollbar、Table基礎 |\n\n| 2026-09-22 | feature/ui-tree-view / PR #246 | TreeViewに縦ScrollbarのTrack/Thumb描画、Trackページ移動、Mouse Captureを使ったThumb Drag、Cancel/Mouse Upでの解放を追加。RavenUITestにScrollbar操作の回帰テストを追加 | GitHub差分確認のみ。Windowsビルド・テスト実行・GPU描画未検証 | Windowsビルドと実機操作確認、Table基礎、Scrollbar外観の共通化 |\n\n| 2026-09-22 | feature/ui-tree-view / PR #246 | UITableの固定幅列・Header固定・行追加/単一選択・上下/Home/End・Wheel縦Scroll・Demo・回帰テストを追加 | TreeViewはユーザーから動作問題なしとの報告。TableはWindowsビルド・実行未検証 | TableのセルClip・列幅変更・Scrollbar、UIテスト実行 |\n\n| 2026-09-22 | feature/ui-tree-view / PR #246 | UITableのセル/ヘッダー文字描画に個別Clipを追加し、UIDrawListのElement Clipと交差合成。列幅API・Header境界Drag Resize・Capture解放・回帰テストを追加 | GitHub差分確認のみ。Windowsビルド・テスト実行・GPU描画未検証。個別Clipは現状未変換の画面座標を使用するため回転/Scale時の検証が必要 | Windows実行確認、Transform対応Clip、Table Scrollbar |\n\n| 2026-09-22 | feature/ui-tree-view / PR #246 | Widget個別ClipをWorld TransformのAABBへ変換してAncestor Clipと交差。UITableに縦ScrollbarのTrack/Thumb描画、Trackページ移動、Thumb Drag、Capture解放と回帰テストを追加 | 前回のTableセルClip/列幅操作はユーザーから問題なしとの報告。今回の変更はWindowsビルド・GPU実行未検証。回転ClipはScissor制約によりAABB近似 | Windowsビルド・UI回帰テスト・Scrollbar/Transform実機確認、Tableの横Scroll/仮想化検討 |\n\n| 2026-09-22 | feature/ui-tree-view / PR #246 | UIScrollBarMetricsを新設しTreeView/TableのThumb長・位置・Drag換算を共通化。GPU不要の境界値テスト追加 | Table ScrollbarとTransform Clipはユーザーから問題なしとの報告。今回の共通化はビルド・テスト未実行 | Windowsビルド・回帰テスト、Table横スクロール |\n\n| 2026-09-22 | feature/ui-tree-view / PR #246 | UITableに横Scrollbar、横Wheel、横Offset API、列描画/Resize座標の横スクロール対応、Trackページ移動・Thumb Dragと回帰テストを追加 | GitHub差分確認のみ。Windowsビルド・実機操作未検証。縦ScrollbarはOverlay表示 | Windowsビルド・回帰テスト、縦横Scrollbar併用確認、大量行描画最適化 |\n\n| 2026-09-22 | feature/ui-tree-view / PR #246 | UITableの可視行区間をViewportから算出し行全件走査を削減。可視列以外のText Layout/Command生成を省略。1万行の可視範囲テスト追加 | 横スクロールはユーザーから問題なしとの報告。今回の最適化はWindowsビルド・実行未検証。行データ自体は全件保持 | Windowsビルド・回帰テスト・大量行描画負荷計測、必要なら外部データモデル/仮想化 |\n\n| 2026-09-22 | feature/ui-tree-view / PR #246 | UITableにRowCountProvider/CellTextProvider外部モデル、GetRowCount、NotifyDataSourceChanged、ClearDataSourceを追加。内部行保持なしで100万行の可視範囲・選択・更新をテスト | GitHub上の実装のみ。外部モデルは呼び出し元が寿命・変更通知を管理し、CellTextは可視セル描画時のみ取得。Windowsビルド・テスト未実行 | Windowsビルド・回帰テスト、実際の100万行スクロール・描画負荷確認 |\n\n| 2026-09-22 | feature/window-system / PR #247（masterへマージ） | OS Window状態・Event、WindowManager、OpenGL共有Context・補助Window描画、Window別VAO、Close Cleanup・Shutdownを実装。Phase 6 / 12の共通OS Window基盤を整備 | ユーザーのWindows実環境で通常動作・終了処理ともに問題なしとの報告。独自UIの論理Window / Multi-Viewport統合は未検証・未実装 | Phase 6の論理UI Window、続いてPhase 12のUIContext・入力・描画接続 |

| 2026-09-22 | feature/ui-drag-drop / PR #249 | UIContextのDrag & Drop基盤、Payload/Preview/Cancel/時間駆動Tick、TreeView同一・別View移動、Root末尾Drop、自動Scroll・Hover展開、挿入線補正、無変更Drop抑制、回帰テストを実装 | ユーザーから各段階のビルド・動作チェックで問題なしとの報告。GitHub Actionsの実行状況は別途確認 | Phase 7の基本機能を区切り、Scene Hierarchy/Undo連携は別PRで検討 |

| 2026-09-22 | feature/ui-tab-system / PR #250 | UITabModel・UITabBar・UITabView、Close/Drag並び替え、Overflow横スクロール、UTF-8タイトル省略、Text Demo、RavenUITest回帰テストを追加 | ユーザーから実環境で動作問題なしとの報告。追加テスト単体の実行結果やGitHub Actionsは別途確認 | Phase 9 Docking SystemのSplit/Tab構造・Dock Preview・Layout保存復元を段階的に設計 |

| 2026-09-22 | feature/ui-docking-system / PR #251 | UIDockLayout / Geometry / Space、入れ子Split、Pane間Tab移動、Dock Preview、空Pane Collapse、JSON Snapshot保存・起動時復元、Backup保護、復元失敗時巻戻し、Demoと回帰テストを追加 | ユーザーからPhase 9の動作チェック問題なしとの報告。GitHub上でmasterとの差分・レビュー未解決0件を確認。RavenUITest単体実行ログとActions実行結果は未取得 | 正式Editorへの組込み、負荷・再入/例外注入テスト、UIElement内部状態の永続化は別課題 |

| 2026-09-23 | feature/ui-theme-style / PR #252 | UIContextのTheme切替、9種類のWidgetの共通Style・状態別配色、個別色優先、RavenUITest回帰テストを追加 | ユーザーから実環境での動作チェック問題なしとの報告。DPIは未実装、テスト単体実行ログ・GitHub Actions結果は未取得 | Phase 10のDPI対応とWindow別Scale/Fontの検証を別PRで進める |

## DPI / Font Atlas診断の実装・検証記録（PR #253〜#269）

- PR #253〜#259: DPI倍率取得、UIContext同期、DIP変換、レイアウト制約・Absolute位置、Label文字幅・Baseline・Glyph倍率。
- PR #260〜#263: DPI別Font Atlasキャッシュ、自動Rebind、Pending一括更新、Application描画準備への接続。
- PR #264〜#268: Font Atlas失敗の分類、再試行抑制、Font容量・不正データ回帰テスト、RHI Device / Resource / Texture upload診断。
- PR #269: GPU Contextを使う独立統合テスト、GPU readbackと異常転送、任意Font Atlas生成。ユーザーによるGPU統合テスト・UI回帰テスト・通常Raven起動の動作確認で問題なし。
- 残課題: 別DPI Monitor移動、OS Window別ContextとMulti-Viewportの入力・描画統合、異常系の自動CI環境整備。

## Phase 11: Immediate Mode風API 実装・検証記録（PR #272）

- [x] UIContext / UIElementのRetained Treeを維持したUIImmediateContextを追加。安定ID、ID/Container Stack、型・親チェック、同一Frame重複検出、宣言順反映を実装。
- [x] 未宣言Widgetを深い順に削除し、Focus / Capture / IMEの解除を既存UIContextへ委譲。AbortFrameでは当該Frameで新規生成したWidgetのみを取り消す。
- [x] Text / Button / SliderFloat / InputText / 専用UICheckbox / BeginPanelを追加。入力結果を次のImmediate宣言で一度だけ消費し、呼び出し側の値と同期する。
- [x] ApplicationSpecificationで明示的に有効化するPhysics DebugパネルをSceneGameへ接続。既存PhysicsDebugSettingsを共有し、8個のCheckbox、2個のSliderと数値ラベルを表示する。
- [x] 既存UI回帰テストへ安定ID・Frame中断・入力・Panel非表示時のFocus解除・再生成・数値同期を追加。ユーザーから各段階の実環境動作チェックで問題なしとの報告を受ける。
- [ ] RavenUITest単体の実行ログ・GitHub Actions結果の個別確認、Popup/ComboBox等へのAPI拡張、正式Editorへの段階的移行は継続課題。
- **契約**: ImmediateのBeginFrame/EndFrameはUIContextの描画Frame前に実行する。Cache内Widgetの外部削除・移譲は行わない。AbortFrameは再利用Widgetの値・順序までは巻き戻さない。

## Phase 12: Window別UI接続・Dock Tab分離（PR #270）

- [x] Window論理座標とFramebuffer実Pixelを分離し、OpenGL Viewport/Scissorと最小化時0 Pixelを扱う。
- [x] 補助WindowごとのUIContext / OpenGLUIRenderer、入力・Focus・IME、DPI Font更新、Window Close時のRenderer解放を接続する。
- [x] Root Widgetの所有権移譲、Dock Tabの新Window切り離しとFrame境界予約、Close時の元Dock Pane復帰とMain Rootへのフォールバックを追加する。
- [x] CPU側のDPI/Context分離/所有権移譲/複数Window逆順Close/Dock消失時の回帰テストを追加。ユーザーから実環境で動作問題なしとの報告を受ける。
- [x] PR #271で論理UIWindowの自動ドラッグ分離・Main領域への再統合、Mouse Up取りこぼし補完を実装。ユーザーから実環境で動作問題なしとの報告を受ける。
- [ ] 異DPIモニター移動、Vulkan/DX12のUI描画Target、Window Close統合テスト、CI結果の個別確認。

| 2026-09-23 | feature/ui-window-framebuffer-dpi / PR #270 | Window別UIContext・Framebuffer実Pixel描画、入力/IME、Dock Tab切り離し/Close復帰、複数Window・復帰先消失のCPU回帰テスト | ユーザーから各段階の動作チェックで問題なしとの報告。CIログ・異DPIモニター移動・他Backendは未確認 | Phase 6の論理Window Widgetと自動分離・再統合、Phase 12の他Backend接続 |

| 2026-09-23 | feature/ui-logical-window-viewport / PR #271 | 論理UIWindowの移動・Resize・前面化、Mainと補助OS Window間のドラッグ分離/復帰、Focus喪失・Mouse Up取りこぼし補完、空補助WindowのClose条件を実装 | ユーザーから実環境で動作チェック問題なしとの報告。GitHub差分レビュー済み。RavenUITest単体ログ・CI結果は未取得 | 異DPIモニター移動、Keyboard Focus/タイトル描画、Window Close統合テスト、他Backend |

| 2026-09-23 | feature/ui-immediate-mode / PR #272 | UIImmediateContextの安定ID・Widget再利用、Text/Button/SliderFloat/InputText/専用Checkbox、Physics Debugパネル、Frame中断・Focus解除・Panel再生成の回帰テストを追加 | ユーザーから各段階の動作チェックで問題なしとの報告。GitHub差分・PR説明文を最終確認。RavenUITest単体ログ・Actions結果は未取得 | Immediate API拡張、正式Editor移行、UIContextとのFrame境界整理 |

## 更新ルール

各PRで該当Phaseのチェックリスト、状態、作業記録、未検証項目を更新する。未着手 → 実装中 → 実装済み（未検証） → 検証済みの順で記録し、後から問題が見つかれば状態を戻す。Phaseの完了は完了条件を満たした場合のみとする。
