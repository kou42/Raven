# Runtime Phase 02 — Scene Structural Safety

2026-11-13。承認対象は既存Scene / ComponentStorage / ComponentViewの構造変更契約と不足テスト。
Runtimeの処理・所有権・公開APIは変更せず、既存SceneViewSelfTestsに回帰を追加した。
Phase 01のEditor修正と調査差分は同じ作業ツリーに残しているが、本Phaseの追加はこの記録、
SceneViewSelfTests.cpp、ロードマップのPhase 02進捗のみ。Phase 03には進まない。

## 所有と参照の流れ

Sceneは型ごとのunique_ptr<IComponentStorage>を所有する。各ComponentStorageはComponentのdense vector、
Entity Indexのdense vector、Index→dense行のsparse vectorを持つ。
Scene::Viewは作成時に型付きStorage pointerを解決し、最小Storageを候補列に選ぶ。
View/IteratorはSceneとStorageへの非所有参照であり、Component配列のアドレスやSnapshotを所有しない。
mapのrehashでもStorageのpointeeは移動しないが、Storage内のvector再確保はComponent参照を無効化する。

Scene自体の破棄後、並行変更中、計測先ComponentViewStatisticsの寿命終了後は使用禁止。
Debug検証もScene pointerを読むため、解放済みSceneを安全に検出する機構ではない。

## 操作と無効化

| 操作 | Component参照 / Storage Iterator | Scene View / View Iterator |
| --- | --- | --- |
| 既存Componentの値編集 | 保持可能（値が所有する内部参照は型の契約に従う） | 保持可能 |
| 対象型への成功したEmplace | 再確保時は全参照を無効化。再確保なしでも古いendは使わない | 対象Storageの走査・再利用は禁止。必ず再取得 |
| 重複Emplace拒否 | 構造変更前に例外 | 保持可能。対象型の汎用経路でテスト |
| Component構築等でEmplace失敗 | 再確保等の可能性があり、成功しなかったことを理由に古い参照を使わない | Debugは保守的に無効化。失敗後も再取得 |
| 存在するComponentのRemove | 削除行と末尾行の参照を無効化。swap-removeで行順が変わる | 候補・非候補を問わず対象Storageなら再取得 |
| 存在しない型/EntityのRemove | 変更なし | 保持可能 |
| Reserveでdense容量拡張 | 全Component参照またはEntity列の参照を再配置し得る | Debug versionも更新。対象Viewは再取得 |
| 容量以下のReserve / ReserveEntities | dense Component参照は保持可能。sparseの内部参照は公開しない | 保持可能 |
| Clear | 全Componentとdense Iteratorを無効化 | version更新。対象Viewを再取得 |
| 無関係な型のStorage追加・値編集 | 対象のComponent列には影響しない | 既存の完全なViewは保持可能。既存テストにmap rehash負荷あり |
| QueueDestroyEntity | この時点では削除しない | 走査中に許可。値編集と併用可能 |
| FlushDestroyedEntities / 即時DestroyEntity | 実際に削除される全型の該当行が影響を受ける | View走査終了後に行い、以降は再取得。空QueueのFlushは変更なし |
| Scene::OnDestroy | Storage自体を解放 | DebugはScene lifetime versionでStorage参照前に拒否。Releaseは旧Viewを触らない |

空のViewも作成時点のStorage集合を保持する。欠けていた型のStorageが後で初めて生成されても、
古い空Viewのnull cacheは更新されず、Debugでもこの追加を必ず検出するわけではない。
構成変更後は古い空Viewを使って結果が更新されると期待せず、Scene::Viewを再取得する。

削除後に参照のアドレスが同じでも、swap-remove先の別Entityの値である可能性がある。
Entityの世代付きHandleを保持し、操作直前に所属・世代を確認してComponent参照を取得し直す。
Index単独の保存や、Component参照のFrameを跨ぐ保持の安全性は保証しない。

## Debug / Releaseの差と例外保証

- DebugはNDEBUG未定義時にStorage structural versionとScene lifetime versionを保存する。
  begin/end、Size/Empty/CandidateCount、Iteratorの増分・逆参照等で検証する。
  Iteratorの等値比較はversion検証を行わない。Componentへのraw参照も自動追跡しない。
- Releaseではversionと検証が省かれる。無効化されたViewを実際に走査して例外を期待するテストは行わない。
  両構成とも同じ禁止事項を守り、変更後の再取得と期待結果を検証する。
- EmplaceはComponent構築後にEntity列への追加が失敗するとComponentをpopし、sparse所属を公開しない。
  vectorの例外保証は型のcopy/move特性に依存する。copy可能またはnoexcept move構築を推奨する。
- RemoveはComponent移動代入が成功してからEntity/sparse対応を更新する。
  移動代入失敗時は索引・件数を維持するが、Component値のロールバックは保証しない。
- Scene全体のCreate/Destroyは複数Storageを跨ぐトランザクションではない。
  noexcept破棄・移動を持つ通常Componentの前提を維持し、例外安全性全体の再設計は行わない。

## 既存検証と追加検証

既存`Root/Tests/SceneStorageTests.cpp`はswap-remove対応、Clear後再利用、move-only所有型の移動、
構築例外・移動代入例外時の索引をassertで検証する。単体スクリプトは/UNDEBUGを指定する。
これは通常Releaseでassertを有効にしたEngine全体の検証を意味しない。

既存`Scene/Tests/SceneViewSelfTests.cpp`はRequireを使い、両構成で走査結果・最小Storage・型順・
map rehash・再取得・破棄予約・世代再利用・診断値・System実行順を確認する。
Debug専用部分は非候補削除、追加、Flush、Scene終了、Reserve/Clearのversionを確認する。

今回の追加:

- Debug: 候補Entityの即時破棄後に旧IteratorとViewを拒否する。
- Debug: Storageを持たない空ViewもScene終了後には拒否する。
- Debug: Component構築失敗後に旧Viewを拒否し、再取得したViewに失敗した所属を公開しない。
- Debug/Release: 重複追加拒否・欠けた型の削除・空Flushは既存Viewを壊さない。
- Debug/Release: 値編集＋重複破棄予約の後、Flushして再取得すると末尾移動先Entityと値が正しい。
- Debug/Release: Index再利用後も古いEntityは復活しない。
- Debug/Release: no-op Reserveとsparse拡張でdense参照を維持し、所有Componentのswap-removeとClearで
  weak_ptrの観測対象資源が解放され、移動した末尾の資源は正しいEntityに残る。

## 既存の更新境界と遅延Commandの判断

Scene::OnUpdateはGame Logic → Animation → Transform評価 → Physics → Layer → Destroy Queue Flushの
既存順序を維持する。Pause時・Transform同期失敗時にも走査を抜けた境界でFlushする。
SceneViewSelfTestsのSystem phase fixtureとLifecycleテストがこれらを検証する。
SceneGameの所有Entity破棄は所有Handleのvectorを走査し、Component Viewそのものを走査しながら破棄しない。
RagdollPhysicsBridgeの解放は既存QueueDestroyEntity経路を使用する。
これらは確認した代表経路であり、全Demo・全callback・並行実行の網羅証明ではない。

現時点で必要性を示す新しい失敗ケースはなく、汎用Deferred Structural Commandは追加しない。
走査中にAdd/Removeが必要になった場合は、Handleと操作内容を呼出側の一時列へ記録し、
走査終了後に生存確認して適用する設計を先に検討する。汎用化が必要なら別途設計承認を受ける。

## 検証結果

- Debug / Release x64ビルドが終了コード0。既存ローカルGLFW native header配置を
  `CL=/I"D:\Engine\artifacts\sg3-deps"`で参照し、Phase 01で確認済みのPDBツール不整合を避けるため
  `/p:RavenGenerateLinkDebugInformation=false /p:LinkIncremental=false`を指定した。
  リポジトリの依存設定は変更せず、Debug / Releaseのコンパイル条件は維持した。
- DebugのリンクWarningは既存LNK4075 / LNK4098の2件、ReleaseはWarning 0件。コンパイルエラー0件。
  ログ: `artifacts/logs/runtime-phase2-Debug-build.log`、`runtime-phase2-Release-build.log`。
- `scripts/test-windows-scene.ps1 -SkipBuild -SkipBootstrap`が終了コード0。
  再コンパイルしたStorage / Transform Mathの2 standalone group、再ビルドDebug exeの10入口が成功。
  新しいDebug無効化検証と共通StructuralContractは`--test-scene-view`に含まれる。
  ログ: `artifacts/logs/runtime-phase2-debug-tests.log`。
- 再ビルドRelease exeの`--test-scene-view`、`--test-scene-runtime`、`--test-editor-scene-lifetime`が
  すべて終了コード0、成功メッセージも確認。ログ: `artifacts/logs/runtime-phase2-release-tests.log`。
- `git diff --check`成功。Raven配下の追加・削除・移動はなく、structure.txt / VS filtersの更新は不要。

GPU実機、全Demo、並行変更、Scene C++ object破棄後のアクセスは対象外。
安全な利用条件とテストの保証範囲を整理し、Phase 02を完了とする。Phase 01の残存リスクは解決済みと扱わない。
