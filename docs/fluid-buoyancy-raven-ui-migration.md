# Fluid Buoyancy Debug HUD Raven UI移行（第1段階）

## 変更
- `OnUpdate`へRキーEdge Resetを移動
- ImGui描画を削除し、UIWindow/UILabel/UIButtonで診断値を表示
- Preset（Water/Heavy Fluid/High Drag/Off）切り替え
- Static/Rigid Coupling切り替え、計測Pause/Start、Body/Measurement Reset
- Scene交換時は毎回Active SceneからFluidWorldを再取得
- OnDetachでHUDのTreeを解放

## 残る機能差分
- 旧HUDのParticle Radius、Restitution、Drag、Pressure Reaction、Buoyancyの直接編集UIは未移植
- 複数Coupling Bindingは先頭Bindingの操作のみ。統計はFluidWorld全体を表示
- Fixed Stepあたりの平均Impulse表示は未移植
- 画面は550x750 DIP固定、スクロール未対応
- RuntimeUIFontの設定とWindows Debugビルド・動作確認が必要

## 確認項目
1. Fluid SceneでBox/Sphereの位置・速度、統計、計測値が更新される
2. Rキー押しっぱなしでResetが連続しない
3. Water/Heavy Fluid/High Drag/Offを押すとPreset適用後にBodyとMeasurementがResetされる
4. Static/Rigid Coupling切り替えが反映される
5. Pause/Start、Reset Measure、Reset Bodiesが動作する
6. Scene切替中はHUDが非表示になり、復帰後に再表示される
7. Dear ImGuiを無効にしてもHUDを操作できる

注意: この段階では旧ImGui HUDとの機能同等性は未達成です。係数Sliderと複数Binding UIを追加してから完全移行と判定してください。
