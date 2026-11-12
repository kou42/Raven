# Raven Scene Transition / UI Navigation Roadmap

## 現在地点

Phase 13までのRuntime基盤を実装済みです。Primary Scene遷移に加えて、Persistent Scene、
Application Lifetime State / Audio / Global UI、Additive Scene、Stage集合Streamingを利用できます。

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

## Phase 7: UI Navigation — 完了

- `UIScreen` 基底クラス
- `UINavigationManager`
- `PushScreen()`
- `PopScreen()`
- `ReplaceScreen()`
- `Clear()`
- `Application::GetUINavigationManager()` からMain UIContext用Navigationへアクセス

SceneとUI Screenの責務を分離します。

- SceneManager: Title / Game / Stageなどゲーム世界そのもの
- UINavigationManager: HUD / Pause / Settings / DialogなどScene上のUI
- SceneTransitionController: Fade / LoadingなどScene交換演出

## Phase 8: Title UI — 完了

- TitleScreen
- Start Game Button → Fade付き `Game` Scene遷移
- Settings Button → SettingsScreen Push
- Exit Button → Application終了要求
- SettingsScreen Back → Screen Stack Pop
- UI callback中のScreen破棄を避けるDeferred Navigation
- EnterによるTitle→Game検証操作をUI Actionへ移行

※ 現在のUIButtonはTextを所有せず、UILabelはFont Atlasの明示指定が必要なため、Button文字表示はPhase 10のFont Asset / LoadingScreen統合と合わせて追加します。

## Phase 9: In-Game UI Navigation — 完了

- HUDScreenをGame SceneのStack底面へ常駐
- PauseScreen
- SettingsScreen再利用
- Resume
- Fade付きTitle復帰
- Screen Stackを利用したPause → Settings → Back
- F10直接Title遷移をPause Menu入口へ移行
- Pause中はGame Logic後のAnimation / Physics / Scene Layer更新を停止
- UI callback中のScreen破棄を避けるDeferred Navigation

## Phase 10: Loading Screen — 完了

- LoadingScreenをUIScreen化 ✓
- Progress表示 ✓
- Loading Message状態 ✓
- Load Error状態 ✓
- SceneTransition Overlayとの統合 ✓
- Immediate spinner / progress barを廃止し、Loading内容をRetained UIへ一本化 ✓
- Application共通Runtime UI Fontの共有 ✓
- Font Asset利用可能時のLoading / Errorテキスト表示 ✓
- Title / Pause / Settings Button文字表示 ✓

※ Font AtlasのGPU生成責務は既存方針を維持し、`ApplicationSpecification::RuntimeUIFont` から共有Atlasを注入します。
Font未指定時も各Screenは従来どおり動作し、Font利用可能時だけUILabelを追加します。

## Phase 11: Async Loading強化 — 基盤完成

- Cancellation ✓
  - cooperative cancellation token
  - callbackがCancel確認を忘れてもScene生成へ進まないController境界の保証
- Load Error型 ✓
  - Cancelled / PreparationFailed / PreparationException / SceneCreationFailed / SceneCreationException
  - LoadingScreenへError種別を反映
- Job Systemとの統合 ✓
  - Application所有の汎用Worker Pool
  - Scene Preparationをstd::asyncからJobSystem::Submitへ移行
  - Controller → JobSystemのshutdown寿命順を保証
- Job System Self Test ✓
  - 戻り値Future
  - 複数Job完了
  - Debug Startupへ接続
- 実ビルド確認 ✓
  - Debug x64 compile / link成功
  - ローカルMSPDB DLL不整合を回避するPDBなし診断build optionを追加
  - Scene Lifetime専用Self Test実行成功
- Asset Loadingとの統合 — 実装中
  - Texture CPU decode専用API ✓
  - decode済みPixelのAsset Manager登録 ✓
  - WorkerではGPU Resourceを生成しない境界 ✓
  - Scene Preparation向けTexture Asset Batch ✓
  - Asset単位Progress / cooperative cancellation ✓
  - Worker Preparation → Main Thread Finalize → Scene Creation ✓
  - Application所有Texture Asset Cacheへfinalize ✓
  - Title → Game実Scene遷移でTexture Asset Batch利用 ✓
  - Legacy OpenGL TextureのMain Thread GPU finalize ✓
  - Game SceneでAsync Load済みAssetを再利用 ✓
  - Direct Scene起動時の同期Load fallback ✓
- Scene生成時のMain Thread stall削減 — 基盤完成
  - Primitive Sphere/CubeのCPU Geometry生成をWorker Preparationへ移行 ✓
  - SceneGamePreparedResourcesでWorker結果をScene生成へ受け渡し ✓
  - Mesh/GPU Resource生成はMain Threadへ維持 ✓
  - Floor/Wave Dynamic GridのCPU Geometry生成をWorker Preparationへ移行 ✓
  - 旧Floor頂点色を専用CreateFloorGeometry()で維持 ✓
  - SceneGame::OnCreate Total / Assets / RenderResources / Entities計測Scope ✓
  - Shader/PipelineはGPU/Backend境界、ECS登録はScene所有境界としてMain Threadに維持 ✓
  - CPU Asset decode / Geometry生成をWorker側へ分割し、次の最適化はRuntime計測値で判断
- PreparationとScene生成を含めたProgress semantics整理 — 完了
  - Preparation完了 80% / Main Thread Finalize完了 90% / Scene生成完了 100% ✓

※ 現在Ravenには汎用Job Systemが存在せず、Texture / RHI Shader Asset Managerも同期Loadです。
`std::async` を直接Job System風APIで包むだけにはせず、Scene Loading以外でも再利用できるJob実行境界を先に設計します。

## Phase 12: Persistent Scene — 基盤完成

- Persistent Scene ✓
- Sceneを跨ぐEntity ✓
  - Entity移送ではなくPersistent Scene所属でScene-local Handle/Storageの所有権を維持
- Audio ✓
  - Application所有の差し替え可能な`IAudioService`境界
- Global UI ✓
  - Scene固有Navigationとは別のApplication Lifetime Stack
- Game / Application State ✓
  - Entity/GPU Resourceを持ち込まない型付き`ApplicationState`

## Phase 13: Additive Scene Loading — 基盤完成

- 複数Scene同時ロード ✓
  - `Persistent -> Primary -> Additive`のUpdate/Render順
  - `Additive -> Primary -> Persistent`のEvent順
  - Explicit RHIでも全SceneのMeshをFrame前にprepare
- `LoadSceneAdditive()` ✓
- `UnloadScene()` ✓
- Scene Instance ID / Deferred Operation Queue ✓
- Stage Streaming ✓
  - Desired Stage ID集合との差分をAdditive Load/Unloadへ変換
- World Streamingへの発展 ✓
  - 距離、Portal、メモリBudget等のPolicyはGame側、Lifetime差分適用はRuntime側へ分離

---

## 次回の推奨実装順

```text
Runtime Profile Capture
  ↓
Stage Streaming Policy（距離 / Portal / Budget）
  ↓
Concrete Audio Backend
  ↓
Async Additive Preparation / Asset Batch統合
```

UI Navigationへ進む前に、現在のScene Transition変更についてVisual Studio / MSBuildで一度ビルド・実行確認を行うことを推奨します。

## 現在の注意点

- GitHub Actions workflowによる自動ビルド確認は現時点ではありません。
- Debug x64のcompile/linkとScene Lifetime Self Testは確認済みです。
- この環境の通常PDB付きlinkは`MSPDB140.DLL`のversion不整合で失敗するため、診断buildでは`RavenGenerateLinkDebugInformation=false`を指定しています。
- Application-owned LayerがScene固有状態を持つ場合は、`OnActiveSceneChanging()` / `OnActiveSceneChanged()`を利用してScene lifetimeを跨いだ参照を保持しないようにします。
- Worker ThreadではScene constructor / Renderer / Physics / ECS初期化を行わず、CPU側Preparationだけを実行します。
- Persistent SceneはPrimary交換では破棄されませんが、Application終了時にはAdditive / Primaryより後に破棄されます。
- Additive Sceneの描画順はLoad順です。透明合成やCamera選択などのWorld固有PolicyはScene側で明示してください。
- AudioはLifetime/Backend境界までで、実際に音声を出すConcrete Backendは未実装です。
