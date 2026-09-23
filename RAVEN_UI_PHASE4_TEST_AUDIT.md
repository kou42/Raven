# Raven UI Phase 4: ComboBox / Popup / Tooltip 実装・検証棚卸し

確認日: 2026-09-23  
対象: `master` の既存実装を確認したうえで、Phase 4の完了条件と未検証項目を分離する。  
注意: この文書の「コード確認」はビルド・テスト実行・GPU実描画の成功を意味しない。

## 既存実装

| 項目 | 実装・接続 | 確認した範囲 |
| --- | --- | --- |
| Popup | `UI/Core/UIContext.h` / `.cpp`、`UI/Core/UIFocusNavigation.inl` | 専用Layer、追加・削除・開閉、Anchor配置、Escape・外側Click、Focus/Capture解除 |
| ComboBox | `UI/Widgets/UIComboBox.h` | 単一選択、選択通知、Mouse/Keyboard操作、PopupのContext所有、Context変更時の解放 |
| Tooltip | `UI/Widgets/UITooltip.h` と `UIContext` | Hover表示、非HitTest、Viewport内配置、Popupとの排他、対象削除時の登録解除 |
| Demo | `UI/Text/Debug/UITextDemoLayer.cpp` | Popup Trigger、ComboBox、両者のTooltip |
| CPU回帰テスト | `Root/Tests/UITextReflowTests.cpp` | `TestPopupRouting`、`TestComboBox`、`TestTooltip` がテスト入口から呼ばれる |

## 既存回帰テストで確認する仕様

- Popup: 通常Widgetより前面のMouse hit、Escape、外側Downの消費、Focus/Capture解除、右端Clamp・上側反転、通常Child削除後の存続。
- ComboBox: 未選択・選択変更通知・同値再設定・不正Index、Enter/Downによる決定、Popup行Mouse選択、Escape、選択肢入替、Widget削除。
- Tooltip: Hover表示、右下Viewport Clamp、Mouse入力透過、Clickで非表示、再Hover、Popup表示時の抑制、対象削除時の登録解除。

## 追加検証が必要な項目

| 優先 | 項目 | 期待する結果 |
| --- | --- | --- |
| 高 | Popupの外側Clickが背後のButtonを誤作動させない | 外側Down/Upの両方を経由して背後のClickが0回 |
| 高 | Popup開閉とFocusの組合せ | 開く前のFocus、Popup内Focus、Escape/外側Click後のFocusが仕様どおり |
| 高 | ComboBoxの境界入力 | 空選択肢、Space、Up/Home/End、同値再選択、Popup開いたまま選択肢更新 |
| 高 | Tooltipの待機時間と移動 | Delay前は非表示、Hover継続で表示、離脱・対象破棄・Popup表示で解除 |
| 高 | Panel ClipとPopup/Tooltipの重なり | Popupが親PanelのClipに切られず、通常UIより前面に描画される |
| 中 | Docking・論理UIWindowとの組合せ | Tab切替・Pane削除・Widget移譲後にPopup/Tooltipの残骸がない |
| 中 | Multi-Viewport | OS Window別ContextでFocus/入力/Overlayの所有権が混線しない |
| 中 | DPI・Viewport端・長い選択肢 | 座標・文字・Popupの配置とClipが実描画で妥当 |
| 中 | Theme / Immediate Mode | 共通Theme反映とImmediate API拡張要否を別途判断 |

## 完了判定の方針

1. 既存テストと追加CPU回帰テストの実行結果を記録する。
2. Windows Debugビルド、Text DemoのMouse/Keyboard/描画確認を記録する。
3. PopupのFocus・閉じる条件・Z順・入力伝播について、上表の基本ケースを満たした時点でPhase 4の基本完了とする。
4. 異DPIモニター・他Backend・正式Editor移行などは、基本完了条件と分けて継続課題として扱う。

次の実装単位: 既存テストの不足ケースを追加し、挙動不一致が再現した箇所だけを修正する。
