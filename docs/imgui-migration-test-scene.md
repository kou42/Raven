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
- [ ] ImGui依存ファイル・呼び出し・project参照を再計測し、結果と計測コマンドを記録する。
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

実行ファイル名・配置先はVisual Studioの出力設定に合わせて調整する。現段階ではDebug限定であり、実際のビルドと起動は未確認。Debug起動時の既存Self Testsは引き続き実行される。

`UISeparator` は独立したヘッダーのみで描画でき、`UITextDemoLayer` のラベルと入力欄の間に配置される。入力イベントを受け付けないため、隣接Widgetの操作を妨げないことを確認する。

検証用Fileメニューは左上の「File」をクリックすると「New」「Save」を表示する。項目選択でログ出力とPopup Closeを行う。Tab/Enter/Space、Escape、Popup外クリックも確認する。現在は汎用UIMenuBarのAddMenu/AddItem APIを利用し、内部でUIButton/UILabel/UIPanelを再利用する。方向キーでMenu切替と項目移動、Home/Endで先頭/末尾移動を行う。実機でのFocus/Popup回帰テストは未実施。

`UICollapsibleSection` はHeaderのButtonとContentを所有するRetained Widget。右側の「Properties」をクリック、またはFocusしてEnter/Spaceで開閉し、閉じたContentがHit Test対象外になることを確認する。

## 手動Smoke Test

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

### 動作確認の記録（2026-10-10）

ユーザーから検証画面の動作チェックに問題がなかった旨の報告あり。実行ログ・ビルド構成・Backend別の確認結果は未提出のため、自動テスト実行ログの取得や全BackendでのExit Gate達成とは区別する。

### ImGui依存の継続計測

リポジトリのルートで `python scripts/count_imgui_dependencies.py --output imgui-baseline.json` を実行すると、製品コードとVisual Studioプロジェクト内のImGui参照ファイル数・文字列参照数・`ImGui::`関数呼び出し数をJSON化できる。コメント・文字列内の参照も含む概数であり、AST解析ではない。計測結果は生成した環境のログとして保持し、PRごとの減少量を比較する。スクリプトはファイルを変更しない。
