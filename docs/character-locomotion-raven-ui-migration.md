# Character Locomotion HUD Raven UI移行

`CharacterLocomotionDebugOverlayLayer`のImGui依存を除去し、Raven UIのRetained Treeへ移行する。

## 実装内容

- `OnAttach`でWindow、診断Label、7つのSlider、Reset/Copy/Print/Save Buttonを作成
- `OnUpdate`でCharacter Snapshotを取得し、診断表示とSliderへ同期
- SliderのCallbackでThresholdとAuthored Motion Speedを既存Character APIへ反映
- Scene交換時はCharacterへの参照を保持せず、操作ごとに再取得
- SliderのSnapshot同期中はCallbackを抑止し、再帰的な変更を防ぐ
- ClipboardコピーはGLFW APIを使用
- `OnDetach`でRoot TreeからWindowを削除

## 動作確認項目

1. Character Sceneで診断値が更新されること
2. Idle/Walk/Run/Sprint Thresholdが最小間隔0.05を維持すること
3. Walk/Run/Sprint Authored Speedが最小間隔0.05を維持すること
4. Reset Thresholds / Reset SpeedsでProfile値に戻ること
5. Copy ConfigがClipboardへ最新値を書き込むこと
6. Print ConfigとSave Profileが正常に動作し、エラーが表示されること
7. Scene切替中はHUDが非表示となり、復帰後に再表示されること
8. ImGui無効の構成でも操作できること

## 注意点

- `RuntimeUIFont`が未設定の場合、文字表示にはFont Atlas設定が必要
- 画面サイズは暫定で510x840 DIP固定。低解像度対応とスクロールは未実装
- 既存のImGui ProgressBarは数値Weight表示へ置換。専用ProgressBar Widgetは今後の課題
- Windows Debugでのビルド・実行検証は未実施
