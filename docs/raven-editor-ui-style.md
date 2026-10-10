# Raven Editor UI Style

## 目的

Editor shellとInspectorで繰り返し使う寸法・装飾色を`UITheme::Editor`へ集約し、個別Panelへ同じ意味のMagic Numberを分散させない。

## 責務

- `UIEditorStyle`はMenu trigger / popup / item、Collapsible Section header、Separatorの標準寸法を保持する。
- Editor固有のpopup背景色とSeparator色も`UIEditorStyle`が保持する。
- `UIContext`はWindow単位で`UITheme`を値保持するため、Editor Styleも別Windowへ暗黙に共有されない。
- Menu、Collapsible Section、SeparatorはContext接続時に標準寸法を取得する。
- Theme変更へ即時追従する必要があるSeparator色は描画時にContextから取得する。`SetColor()`を呼んだ場合だけ個別指定を優先する。

## 利用方針

新しいEditor Panelでは、共通する行高、余白、装飾色をPanel内の数値リテラルとして追加しない。既存の`UIEditorStyle`と同じ責務ならその値を利用し、異なる責務なら用途が分かる名前で`UIEditorStyle`へ追加する。

Asset固有の表示値、Gameplay設定、Runtime調整値はEditor外観ではないため`UIEditorStyle`へ入れない。
