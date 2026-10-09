# 脱ImGui検証シーン（Phase 0 / Phase 1）

## 目的

既存の `UITextDemoLayer` / `UISvgDemoLayer` / `RavenUITest` / `RavenUIGPUTest` を活用し、Dear ImGuiを使わずにRaven UIのEditor移行要件を検証する。新たなUI基盤やScene-owned Runtime参照を増やさない。

## 現在の検証入口

- Debug通常起動では `main.cpp` が `UITextDemoLayer` と `UISvgDemoLayer` を登録する。
- `UITextDemoLayer` はText、Input、Tree、Table、Tab、Dockingの既存Demoを提供する。
- `RavenUITest=true` はCPU回帰テスト、`RavenUIGPUTest=true` はGPU統合テストのビルド入口。両方同時には指定しない。
- 既存EditorはDear ImGuiを使用するため、**通常起動にRaven UI Demoがあることは「脱ImGui単独起動」の証明にならない**。

## 専用テストシーンの段階的な構築

### A. Phase 0: 隔離した起動とBaseline

- [x] `--ui-migration-test` 起動モードを追加。DebugのOpenGL経路で既存Runtime Scene / EditorLayer / Dear ImGuiを登録・生成しない。
- [x] `UITextDemoLayer` / `UISvgDemoLayer` を再利用し、`Application::Run()` を共有する。
- [x] `scripts/measure-imgui-dependencies.ps1` でImGui依存ファイル・API呼び出し・project参照・実行時生成条件を再計測できる。
- [ ] Mouse / Keyboard / Clipboard / IME / Focus / Captureの手動Baselineを記録する。

### B. Phase 1: Editor shell検証

- [ ] Menu / Collapsible Section / Separatorを棚卸しし、不足分だけ実装する（Separator、UICollapsibleSection、検証用Fileメニューを追加済み。汎用UIMenuBarとAddItem APIは実装済み（方向キーNavigationを追加済み（自動テスト未実施）））。
- [ ] Dock PaneへTree、Table、Input、診断値を配置する。
- [ ] Viewport用RenderTarget Imageをnative handle非依存の契約で表示する。
- [ ] Focus / Capture / Shortcut優先順位のCPUテストを追加する。
- [ ] Dock layout保存・復元、破損時fallback、Tab分離・復帰を検証する。

### C. Backend検証

| 対象 | OpenGL | DirectX 12 | Vulkan |
| --- | --- | --- | --- |
| UI Widget / Font / Clip | 未検証 | 未検証 | 未検証 |
| RenderTarget Image / UV / Aspect | 未検証 | 未検証 | 未検証 |
| Input / Focus / Capture | 未検証 | 未検証 | 未検証 |
| Docking / Multi-Viewport | 未検証 | 未検証 | 未検証 |

## 起動方法

Debug構成で通常のRaven実行ファイルをビルドし、作業ディレクトリを既存の実行環境と同じにして次を実行する。

```powershell
& "./Root/Root/x64/Debug/Root.exe" --ui-migration-test
```

Editor UI Backendは通常のOpenGL起動時に次の引数で選択する。`Dual`はPanel単位の移行比較用であり、各Panelの所有Backendは`EditorLayer::PanelUIOwnership`で一意にする。現時点では未移行のEditor PanelはDear ImGui所有である。

```powershell
& "./Root/Root/x64/Debug/Root.exe" --editor-ui=imgui
& "./Root/Root/x64/Debug/Root.exe" --editor-ui=raven
& "./Root/Root/x64/Debug/Root.exe" --editor-ui=dual
```

依存数は次のコマンドで再計測する。JSONを残す場合だけ`-OutputJson`を指定し、通常は作業Treeへ生成物を残さない。

```powershell
& "./scripts/measure-imgui-dependencies.ps1"
& "./scripts/measure-imgui-dependencies.ps1" -OutputJson ".tmp-imgui-baseline.json"
```

`--editor-ui=raven`はDear ImGui Contextを生成せず、Raven UI所有へ移行済みのEditor Panelだけを有効にする。未移行Panelを暗黙にDear ImGuiへfallbackしないため、移行途中ではEditor全機能が揃わないことを正常な診断状態として扱う。

2026-10-10のBaselineは、依存ファイル41、`ImGui::*`呼び出し411、project参照行14、実行時生成条件行14。依存ファイル数はコメント内の`ImGui`表記も含む保守的な値であり、Phase 8の0件判定と同じ除外条件（Markdown、vendor、生成物を除外）を使用する。

同日の通常Debug x64インクリメンタルビルドは、ローカルのMSPDB不整合を避ける既存診断設定`/p:RavenGenerateLinkDebugInformation=false`で成功した。既存の`LNK4098`（MSVCRT競合）Warningが1件残る。`RavenUITest=true`も同条件でビルド成功したが、実行は既存の`UIImmediateContext`宣言順テスト（`declaration order applied`）で終了コード1となったため、CPU回帰Baselineの成功扱いにはしない。`RavenUIGPUTest=true`は`Root/Tests/UIFontTextureGPUIntegrationTests.cpp:107`の既存`getenv`に対するC4996でコンパイル失敗した。手動Editor Smoke Testは未実施。

実行ファイル名・配置先はVisual Studioの出力設定に合わせて調整する。現段階ではDebug限定であり、実際のビルドと起動は未確認。Debug起動時の既存Self Testsは引き続き実行される。

`UISeparator` は独立したヘッダーのみで描画でき、`UITextDemoLayer` のラベルと入力欄の間に配置される。入力イベントを受け付けないため、隣接Widgetの操作を妨げないことを確認する。

検証用Fileメニューは左上の「File」をクリックすると「New」「Save」を表示する。項目選択でログ出力とPopup Closeを行う。Tab/Enter/Space、Escape、Popup外クリックも確認する。現在は汎用UIMenuBarのAddMenu/AddItem APIを利用し、内部でUIButton/UILabel/UIPanelを再利用する。方向キーでMenu切替と項目移動、Home/Endで先頭/末尾移動を行う。実機でのFocus/Popup回帰テストは未実施。

`UICollapsibleSection` はHeaderのButtonとContentを所有するRetained Widget。右側の「Properties」をクリック、またはFocusしてEnter/Spaceで開閉し、閉じたContentがHit Test対象外になることを確認する。

## 手動Smoke Test

### 現行Dear ImGui Editorの比較Baseline

移行したPanelは同じ項目をこの順序で再確認する。結果は「未実施」「成功」「失敗」を区別し、操作できることだけでなくEditor状態とRuntime状態が一致することを確認する。

1. Scene HierarchyでEntityを選択し、Inspector、Scene ViewのOutline、Gizmoが同じEntityを指すことを確認する。
2. InspectorでTransformを変更し、値とScene描画が同一frameで追従すること、Undo/Redoで変更前後へ戻ることを確認する。
3. Entity名を変更し、Hierarchy表示とUndo/Redoが一致することを確認する。
4. Scene Viewの映像内をクリックしてEntity Picking位置が一致し、映像外では選択が変わらないことを確認する。
5. Translate / Rotate / Scale Gizmoを操作し、Drag完了が1つのUndo Commandになることを確認する。
6. Scene View / Game ViewをResizeし、Framebuffer表示の向き、Aspect、Clip、Picking位置を確認する。
7. 各PanelをDock移動・Tab切替・表示切替し、再起動後に`imgui.ini`から配置が復元されることを確認する。
8. Text入力中、Gizmo Drag中、Scene View Camera操作中にShortcutやRuntime入力が競合しないことを確認する。

2026-10-10時点では上記手順を記録した段階で、実環境でのDear ImGui比較Baselineは未実施。

### Raven UI単独経路

1. 単独起動でImGui Contextが生成されないことを確認。
2. Button、Checkbox、Slider、InputText、Tree、Table、Tabを操作。
3. Popup表示中の外側ClickとKeyboard Focus、Text入力中のShortcutを確認。
4. Dock移動・分割・保存・再起動後の復元を確認。
5. Window Resize、最小化、DPI変更、補助Window分離・復帰を確認。
6. Viewport画像のClip、UV方向、Letterbox外Click除外を確認。

## 記録方法

各項目は「コード確認」「自動テスト実行」「実環境手動確認」を区別する。実行していないテストを成功扱いにしない。

## 実装上の注意

`Application`にEditor固有Panelを直接追加しない。既存`UIContext`の所有権・Frame境界を守り、UIからRuntime Sceneへの書き込みは将来のEditor Command経由に限定する。

### CPU回帰テスト（追加）

`Root/Tests/UITextReflowTests.cpp` に `TestMenuBarKeyboardAndLifetime` と `TestCollapsibleSectionVisibility` を追加しました。MenuBarのDown/Enter、RightによるMenu切替、PopupのDetach時回収、Sectionの開閉時Visibilityを確認します。`RavenUITest=true` のテスト実行入口へ登録済みですが、MSBuildおよび実行結果は未確認です。
