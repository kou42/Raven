# Fluid Buoyancy Debug HUD Raven UI移行

## 実装済み

- ImGui表示をUIWindow/UILabel/UIButton/UISliderへ移行
- RキーのEdge Resetと診断表示更新をOnUpdateで実行
- Water/Heavy Fluid/High Drag/OffのPreset
- Static/Rigid Couplingの切り替え、計測Pause/Start、Body/Measurement Reset
- 5係数Slider: Particle Radius、Restitution、Drag、Pressure Reaction、Buoyancy
- Particle RadiusとRestitutionはStatic/Rigidの両方に同期
- 複数BindingをNext Bindingで巡回選択し、選択BindingへPreset・Toggle・Sliderを適用
- SliderのSnapshot同期中はCallbackを抑止し、不要な書き戻しを防止
- Scene交換時はBindingのポインタを保持せず再取得
- OnDetachでHUD Treeを破棄

## 動作確認

1. Fluid SceneでBox/Sphereの位置・速度、統計、計測値が更新される
2. Rキー押しっぱなしでResetが連続しない
3. Preset適用後にBodyとMeasurementがResetされる
4. Static/Rigid Coupling切り替えが反映される
5. 5係数Sliderが正しい値を更新し、Particle RadiusとRestitutionはStatic/Rigidで同期する
6. 複数Binding時にNext Bindingで選択先が変わり、選択したBindingだけを変更できる
7. Pause/Start、Reset Measure、Reset Bodiesが動作する
8. Scene切替後も旧Sceneへの参照を使わず表示が復帰する
9. Dear ImGuiを無効にしてもHUDを操作できる

## 注意点

- UIは550x990 DIP固定。低解像度向けスクロールは未対応
- 固定Stepあたりの平均Impulse表示は未移植
- 複数Bindingは同時表示ではなく選択方式
- RuntimeUIFont設定とWindows Debugビルド・実機動作確認は未実施
