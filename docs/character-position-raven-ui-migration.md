# Character Position Debug HUD Raven UI移行

Phase 2の最初の移行対象として、`CharacterPositionDebugOverlayLayer` のDear ImGui描画をRaven UIの`UIWindow`、`UILabel`、`UIButton`へ置き換えました。

- `OnAttach`: Root UI TreeにHUDを追加する
- `OnUpdate`: Fキーの立ち上がり検出と座標・距離表示を更新する
- `OnDetach`: HUDをTreeから削除して非所有ポインタを破棄する
- Teleport: Button/KeyboardともにActive SceneからCharacter Layerを再解決する
- Sceneなし: HUDを非表示にし、Scene復帰時に再表示する

## 動作確認

1. Character SceneでHUDが表示され、X/Y/Zと距離が変化すること
2. Fキーを押し続けてもTeleportが連続発火しないこと
3. Teleportボタンのマウス操作とEnter/Space操作が有効なこと
4. Scene切替中にクラッシュせず、復帰後に表示が再開すること
5. HUDをドラッグして移動できること
6. Dear ImGui無効の構成でHUDの表示と操作が可能なこと

注意: `ApplicationSpecification::RuntimeUIFont` が未設定の場合、UILabelの文字描画は利用可能なFont Atlasに依存します。フォントの初期化とWindows Debugでの動作確認は未実施です。HUDは固定サイズのため、低解像度Windowでの初期位置は今後調整が必要です。
