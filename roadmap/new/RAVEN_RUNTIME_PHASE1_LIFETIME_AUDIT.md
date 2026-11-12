# Runtime Phase 01 — 既存Scene Entity寿命調査

調査日: 2026-11-13。対象は既存Sceneの契約・テスト・参照保持経路の確認。
独立RuntimeWorld、Entity allocator、Component Storageは追加しない。
E1は調査後のユーザー承認に基づき下記の個別範囲で修正する。E2/E3は未修正。Phase 02には進まない。

## 寿命と所有権の契約

| 対象 | 正規所有者・保証・制約 |
| --- | --- |
| EntityHandle | `Scene/Entity.h`の32bit Index + 32bit Generation。Index 0が無効。`IsValid()`はIndexの形式確認のみで、生存やScene所属を保証しない。初回Generation 0は有効。 |
| Entity | Handle + 非所有Scene pointer。Sceneの寿命を延ばさない。Scene破棄後はbool変換・Component操作を禁止する。Handleの値だけを取得できても、生存は確認できない。 |
| CreateEntity | `Scene/Scene.cpp`。Free List末尾を再利用し、なければSlotを追加。TagとTransformを生成する。Component構築失敗を含むトランザクション保証はない。 |
| DestroyEntity | Scene所属・Index・Generation・Aliveを検証してから子の切離し、Component削除、Transform cache破棄、Alive解除、Generation加算、Free List登録を行う。 |
| IsEntityAlive | Entity版はScene所属も検証。Handle版とIndex/Generation版は呼出先Scene内だけの判定で、他Scene由来の値を識別できない。 |
| 破棄予約 | QueueはEntity全体を保存し、Flushで再検証する。重複予約や即時破棄後の予約が再利用先を破棄しない。Scene更新・Pause時にもFlushする。 |
| Index Component API | `Scene/Scene.inl`。現在のSlotのAliveを検証するがGenerationと所属は検証しない。Index再利用後は新しいEntityのComponentを取得する。長期間保存した参照の解決には使わない。 |
| Entity Component API | bool変換でSceneの生存Slot・Generationを検証してからIndex APIに委譲する。Scene自体が破棄済みの場合を救済する機構ではない。 |
| Scene終了 | SceneManagerが派生OnDestroy → 基底OnDestroy → unique_ptr解放。基底はLayer逆順Detach → Queue破棄 → 生存Entity sweep → Storage/cache解放。基底OnDestroyの冪等性は既存テストで確認する。 |

`Entity`と裸の`EntityHandle`は別の保証を持つ。所属を検証する入口でEntityを受け取った後、
同期処理中にだけIndexへ分解する使い方と、Indexを保存して後で現在世代を再取得する使い方を区別する。

## 参照保持とデータフローの確認

- `SceneManager.h/.cpp`: Primary / Persistent / Additiveを所有し、予約変更はFrame末尾のFlushで適用する。
  SceneInstanceIDはManager内の識別子でありEntityHandleには含まれない。即時SetSceneは起動などFrame外用。
- `Core/Application.cpp`: Update・描画・Present完了後にPrimary変更前通知、Scene操作Flush、変更後通知を行う。
  変更前通知では旧Sceneが生存している。EditorはE1修正でここから解除する。他の全Layerが解除する保証ではない。
- `Editor/EditorCommandHistory.cpp`: 非所有Scene pointerとCommandを保持し、OnUpdateからのScene切替検出でHistoryをClearする。
  `DeleteEntityCommand.cpp`等は対象SceneとGenerationを検証してからIndex APIを使う。
- `Editor/EditorLayer.cpp`: 選択EntityをFrame間保持する。Explicit Pickingは要求SequenceとScene pointerを保持し、
  結果消費時に両者を照合する。調査時はActiveScene変更前通知をoverrideしていなかった（下記E1で修正）。
- `Scene/SceneTransitionController.h/.cpp`: WorkerにはPreparation callbackとshared LoadingContextを渡す。
  終了時はfutureをwaitし、Scene生成はApplication threadで行う。API上はcallbackによるScene捕捉を禁止できない。
- `Scene/SceneTitle.cpp`、`SceneGamePreparation.h`: 現行Preparationは共有CPU Resourceを捕捉し、EntityをWorkerへ渡さない。
  Main-thread finalize / Scene生成callbackはSceneTitleのthisを捕捉するため、Controllerとの終了順が前提。
  Application所有Sceneに対して任意WorkerからEntity APIを呼ぶ安全性や排他は保証されない。
- Index API利用例: `PhysicsWorld::AddForce/AddImpulse/WakeUp`はEntityの所属・世代確認後にTryGetComponentへ進む。
  Scene View中のEntityからの同期取得も保存Indexの後日解決とは異なる。全呼出箇所の安全性を証明したものではない。

## 既存テストの保証範囲

`Scene/Tests/SceneLifecycleSelfTests.cpp`のRunSceneLifecycleSelfTestsを基準に確認した。
同じ振る舞いを重複する新しいテストは追加しない。

| ケース | 既存確認 | 限界 |
| --- | --- | --- |
| 別SceneのEntity | 同一HandleのEntityを作り、IsAlive / Destroy / Queueの拒否を確認 | 裸HandleのScene混用は検出対象外 |
| 二重破棄・重複Queue | 即時破棄と二重予約を混在させても再利用Entityが生存 | 並行実行は対象外 |
| Index再利用・stale handle | 同じIndex・別Generation、古いEntityのHas/Remove失敗とGet/Add例外を確認 | Generation周回は未検証 |
| 無効Index | 0と最大IndexのTryGet、破棄SlotのGet/Add/Has/Removeを確認 | 全API・全整数値の網羅ではない |
| View内の破棄予約 | 値変更とQueue後は生存、Flush後に空になることを確認 | 走査中の即時構造変更を許可しない |
| 終了・再終了 | Layer LIFO、即時/予約/残存Entity回収、OnDestroy再実行を確認 | Scene C++ object破棄後のアクセスはテストしない |
| SceneManager | Persistent / Primary / AdditiveとDeferred操作の所有順を確認 | Editorが選択保持した状態での交換は対象外 |

Lifecycleテストはassertを使い、mainの起動入口もDebug限定。
通常Releaseのビルド成功や未知のテスト引数での通常起動をLifecycle合格として扱わない。
SG5のRuntime比較はReleaseでもRequireを使うが、EditorのScene交換やPickingを検証するものではない。

## GPU Pickingの世代復元

OpenGLは`EditorEntityPicking.cpp`でR32IにIndexを書き、EditorLayerのReadPixelで読む。
DX12 / VulkanはOffscreenRenderTargetのSequence付きreadback結果をEditorが後続更新で消費する。
EditorのApplyPickedEntityIDは現在のSceneからGenerationを取得してEntityを再構築する。

FenceやSequenceはGPU完了・要求の対応を保証するが、描画時点のEntity Generationは伝えない。
描画したAを破棄 → 同じIndexでB生成 → Aの結果消費、という順序ではBを選択し得る。
OpenGLでも前回描画のIDと次の再描画までの構造変更が問題となり得る。
範囲外Indexの例外処理やAlive確認だけではこの誤対応を検出できない。
GPU testの成功は同じIndexの再利用競合の保証と同義ではない。本調査でGPU実機再現はしていない。

## 確認した不足と個別修正候補

### E1: Scene交換後のEditor選択参照（承認済み修正）

`ValidateSelectedEntity`は`static_cast<bool>(m_SelectedEntity)`を所属Scene比較より先に呼ぶ。
bool変換はEntity.cppからm_Sceneを逆参照する。Applicationは旧Sceneを破棄してから次Frameに進み、
EditorLayerには変更前解除のoverrideがないため、選択を保持してPrimaryを交換すると解放済みSceneへ到達する経路がある。
解放領域のアドレス再利用による誤一致も残る。静的に確認した経路であり、実機クラッシュを再現したとの意味ではない。

修正: EditorLayerのOnActiveSceneChangingで選択・Gizmo・History・Picking要求とPanelの旧Snapshotを解除し、
ValidateSelectedEntityは所属を先に確認する。通知はApplicationの既存経路で旧Scene破棄前に呼ばれる。
対象はEditorLayer.h/.cpp、既存EditorEntityCommandSelfTests.h/.cpp、main.cppとSceneテストスクリプト。
RunEditorSceneLifetimeSelfTestsは旧Scene生存中の解除、同じアドレスとHandle値でのScene再生成、
Pending Picking情報の解除、ドラッグとUndoの無効化、選択の世代確認をCPUのみで検証する。
専用入口は`--test-editor-scene-lifetime`。Require相当の例外判定でDebug/Release両方に対応する。
SceneManagerの所有権やEntity allocatorは変更しない。通知を通らない即時SetSceneはFrame外起動用の既存契約を維持する。

### E2: Pickingでの同Index別Entity選択

候補: 描画SnapshotごとのIndex→Generation対応とScene instanceの識別を結果消費まで保持するか、
構造変更後の結果を失効させる。要求時の世代取得だけでは描画Snapshotとの一致を保証できない。
変更対象はEditor / Snapshot / Backend間の契約に及ぶ可能性があるため、E1と分けて設計承認する。

### E3: Generation / Index上限と例外保証

DestroyEntityのuint32 Generation加算は周回し、同Slotを2^32回破棄すると古いHandleが一致し得る。
新Slot Indexのsize_t→uint32変換にも上限検証がない。HandlePoolのSlot退役方針は参考になるが別allocatorへ置換しない。
Create時のComponent構築例外やDestroy時のStorage移動例外もEntity全体を巻き戻さない。
これらは通常の既存テストで保証していない残存リスクとして記録し、本調査で一括修正しない。

## 検証記録と完了状態

- Debug x64: `scripts/build-windows.ps1 -Configuration Debug -SkipBootstrap`を実行。
  WindowsWindow.cpp:17のC1083（`GLFW/glfw3native.h`不足）を確認。ビルド成功ではない。
  ログ: `artifacts/logs/runtime-phase1-debug-build.log`。
- 同じ依存が必要なReleaseの再ビルドは未実施。依存配置の変更は本調査に含めない。
- `scripts/test-windows-scene.ps1 -SkipBuild -SkipBootstrap`: 終了コード0。
  Storage / Transform Mathの2 standalone groupと、Lifecycle / Transform / Runtime / View /
  Simulation / Physics Collision / Graph / Editor Commands / Graph計測の9入口が成功。
  standaloneは今回コンパイル。Engine側は再ビルド前の既存Debug exe（更新日時2026-11-13 00:36:50）であり、
  現在のソース全体のビルド成功やE1/E2の再現検証を意味しない。
  ログ: `artifacts/logs/runtime-phase1-scene-tests.log`。
- Runtimeソースと既存テストは変更していない。GPU実機、Scene交換時のEditor操作、
  Generation上限到達、Release Lifecycleの検証は未実施。

上記は調査時点の検証記録。E1修正後の結果は以下に記す。

### E1修正後の検証

- Debug x64: ローカル既存`artifacts/sg3-deps`をCLのincludeへ追加してコンパイル。
  通常リンクはLNK1101（MSPDB140.DLLの不整合）で失敗したため、既存プロパティ
  `/p:RavenGenerateLinkDebugInformation=false /p:LinkIncremental=false`で再実行し、終了コード0。
  ソースのDebug条件は維持し、リポジトリの依存・コンパイラ設定は変更していない。
  再リンク時WarningはLNK4075とLNK4098。最終ログ: `artifacts/logs/runtime-phase1-e1-Debug-final-build.log`。
  初回コンパイルには既存AstroテストのC4244が9件あり、変更対象ソースからのWarningは0件。
- 再ビルドしたDebug exeで`test-windows-scene.ps1 -SkipBuild -SkipBootstrap`が終了コード0。
  Storage / Transform Mathの2 standalone groupと、追加したEditor寿命を含む10入口が成功。
  ログ: `artifacts/logs/runtime-phase1-e1-debug-tests.log`。
- Release x64も同じPDB不整合で通常リンクが失敗した後、同じローカル指定で最終ビルドが終了コード0。
  最終ログ: `artifacts/logs/runtime-phase1-e1-Release-final-build.log`。最終ビルドのWarningは0件。
  新規`--test-editor-scene-lifetime`も終了コード0、成功メッセージを確認。
  ログ: `artifacts/logs/runtime-phase1-e1-release-tests.log`。通常ReleaseでDebug専用Lifecycle全体を実行したものではない。
- 最初のRelease実行は、未AttachのTree選択を確認するテストの誤りを検出。
  CPUテストでは保持SnapshotのNode数を確認するよう修正し、最終ソースを両構成で再ビルドして上記テストを再実行した。
- `git diff --check`成功。Raven配下の追加・削除・移動はなく、structure.txt / VS filtersの追加登録は不要。
- 新規寿命テストはGPU/UIをAttachせず、Layer仮想通知の解除処理を呼ぶCPU検証。
  実機Scene交換操作、描画結果、同一Scene内のGPU Picking世代競合を検証したものではない。

調査とE1の実装は完了。E2/E3は未修正。
Phase 01全体を修正済み・全保証済みとして扱わない。Phase 02へは進まない。
