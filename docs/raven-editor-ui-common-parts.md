# Raven Editor UI共通部品と入力優先順位

## Widget棚卸し

| Editor用途 | Raven UI部品 | 方式 | 判断 |
| --- | --- | --- | --- |
| Menu Bar / Menu / Menu Item | `UIMenuBar` | Retained | Popup、Keyboard Focus、項目Lifetimeを持つためRetainedとする。 |
| Collapsible Section | `UICollapsibleSection` | Retained | 展開状態とContent TreeをFrame間で保持する。 |
| Separator | `UISeparator` | Retained | Editor Layout内の装飾要素として配置する。 |
| Table / Tree | `UITable` / `UITreeView` | Retained | Selection、Scroll、Resize、Drag & Drop状態を保持する。 |
| Text / Button / Checkbox / Slider | `UIImmediateContext` | Immediate | 毎Frame更新されるDebug値と単純操作に利用する。 |
| Input Text | `UIImmediateContext::InputText`または`UIInputText` | 用途別 | Debug OverlayはImmediate、Inspectorの編集状態はRetainedを使う。 |
| Panel / Dock Pane / RenderTarget Image | `UIPanel` / `UIDockSpace` / `UIImage` | Retained | Layout、Focus、Texture Lifetimeを安定して保持する。 |

Editor shellとInspectorはRetained Widgetを基本とし、`UIImmediateContext`は診断値中心のDebug Overlayへ限定する。ImGui互換APIを再現せず、状態のLifetimeに応じて両方式を使い分ける。

## Interaction問い合わせ

`UIContext::GetInteractionState()`はPanelまたはViewportのSubtreeについて、次の状態を集約する。

- `Hovered`: PointerのHit TargetがSubtree内にある。
- `Focused`: Keyboard Focus所有者がSubtree内にある。
- `Captured`: Mouse Capture所有者がSubtree内にある。
- `Active`: Pressed、Mouse Capture、Drag SourceのいずれかがSubtree内にある。

別ContextまたはTreeへ未接続のElementは、全てfalseの状態を返す。これによりEditor側がUI Core内部のraw pointerを比較したり、Widgetの子構造を走査したりせずに入力所有権を判断できる。

## Editor Shortcut優先順位

`ResolveEditorShortcutTarget()`は次の順序でShortcut所有者を一意に決める。

1. Popup
2. Text Input
3. Gizmo
4. Viewport
5. Panel
6. Global

Popup表示中はEscapeやNavigationを背後へ渡さない。Text入力中はCopy、Paste、Undo、Delete等をEditor全体の操作より優先する。Gizmo操作中はViewport Cameraや選択操作へ同じ入力を重複配送しない。ViewportとPanelはFocus、Active、Captureのいずれかを持つ場合だけShortcutを所有し、それ以外はGlobal Shortcutへ委譲する。

`UIContext`は汎用的なInteraction StateとText入力意図だけを公開し、Editor固有の優先順位は`Raven/Editor/EditorShortcutRouter`へ置く。Game UIはこの優先順位へ依存しない。
