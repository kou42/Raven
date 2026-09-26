# Raven Scene Transition / UI Navigation Roadmap

## 現在地点

Scene Transition基盤とTitle / Game間の実Scene遷移まで実装済みです。

## Phase 1: Scene Lifetime基盤 — 完了

- SceneManager
- Active Scene所有
- Deferred Scene Change
- Frame安全境界でScene交換

## Phase 2: Scene Transition — 完了

- Instant Transition
- FadeOut / FadeIn
- Transition中のInput Block
- Loading Overlay

## Phase 3: Scene Factory / ID — 完了

- SceneFactory
- `Game` Scene ID
- `Title` Scene ID
- IDベースScene生成

## Phase 4: Async Loading — 基盤完成

- Worker ThreadでCPU側Preparation
- Application ThreadでScene生成
- SceneLoadingProgress
- Progress Bar
- Loading Indicator Animation

## Phase 5: Scene交換時のLayer Lifetime — 基盤完成

- `OnActiveSceneChanging()`
- `OnActiveSceneChanged()`
- `Scene::FindLayer<T>()`
- Character Debug OverlayのActive Scene追従
- SoftBody / Fluid Demo Layerのcleanup / rebuild
- SceneGame専用Demoを非Game Sceneへ生成しないguard

## Phase 6: 実Scene遷移 — 完了

- SceneTitle
- Game → Title（F10）
- Title → Game（Enter）
- Scene同士は具体型ではなくScene IDで遷移要求

---

## Phase 7: UI Navigation

次回はここから開始します。

1. `UIScreen` 基底クラス
2. `UINavigationManager`
3. `PushScreen()`
4. `PopScreen()`
5. `ReplaceScreen()`
6. `Clear()`

SceneとUI Screenの責務を分離します。

- SceneManager: Title / Game / Stageなどゲーム世界そのもの
- UINavigationManager: HUD / Pause / Settings / DialogなどScene上のUI
- SceneTransitionController: Fade / LoadingなどScene交換演出

## Phase 8: Title UI

- TitleScreen
- Start Game Button
- Settings Button
- Exit Button
- Enter / F10による検証操作からUI Actionへ移行

## Phase 9: In-Game UI Navigation

- HUD
- PauseScreen
- SettingsScreen
- Resume
- Titleへ戻る操作
- Screen Stackを利用したPause → Settings → Back

## Phase 10: Loading Screen

- LoadingScreenをUIScreen化
- Progress表示
- Loading Message
- Load Error表示
- SceneTransition Overlayとの統合
- Font Asset利用可能時のテキスト表示

## Phase 11: Async Loading強化

- Cancellation
- Load Error型
- Job Systemとの統合
- Asset Loadingとの統合
- Scene生成時のMain Thread stall削減
- PreparationとScene生成を含めたProgress semantics整理

## Phase 12: Persistent Scene

- Persistent Scene
- Sceneを跨ぐEntity
- Audio
- Global UI
- Game / Application State

## Phase 13: Additive Scene Loading

- 複数Scene同時ロード
- `LoadSceneAdditive()`
- `UnloadScene()`
- Stage Streaming
- World Streamingへの発展

---

## 次回の推奨実装順

```text
UIScreen
  ↓
UINavigationManager
  ↓
TitleScreen
  ↓
Title UI Action → SceneTransitionController
  ↓
Pause / Settings
  ↓
LoadingScreen
```

UI Navigationへ進む前に、現在のScene Transition変更についてVisual Studio / MSBuildで一度ビルド・実行確認を行うことを推奨します。

## 現在の注意点

- GitHub Actions workflowによる自動ビルド確認は現時点ではありません。
- Scene Transition変更は静的確認済みですが、ローカルVisual Studio / MSBuildによるビルド・実行確認は未実施です。
- Application-owned LayerがScene固有状態を持つ場合は、`OnActiveSceneChanging()` / `OnActiveSceneChanged()`を利用してScene lifetimeを跨いだ参照を保持しないようにします。
- Worker ThreadではScene constructor / Renderer / Physics / ECS初期化を行わず、CPU側Preparationだけを実行します。
