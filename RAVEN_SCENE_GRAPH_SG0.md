# Scene Graph SG0 — 安全性のベースライン

2026-11-04。SG0の変更とCPUテストを追加。Windowsビルド・Scene Lifecycleの実行・
Editor/Physics/AnimationのRuntime回帰確認は未実施のため、マイルストーンの検証完了は保留。
SG1以降には進まない。

## 所有権とアクセス境界

| 対象 | 所有と有効期間・アクセス契約 |
| --- | --- |
| Scene | SceneManager等が所有。Entity Slot、ComponentStorage、Scene Layer、PhysicsSimulationWorldを所有する。 |
| Entity | Handleと非所有Scene pointer。Scene存命中のみ使用可能。Component操作は世代を検証してSceneへ委譲する。Scene破棄後の安全性は保証しない。 |
| EntityHandle | Index + Generation。Scene情報を含まずScene内だけで有効。Handleを受け取る親設定やSnapshot APIでは呼び出し側がScene所属を保証する。 |
| IsEntityAlive(Entity) | Scene pointer、Index、Generation、生存Slotをすべて照合する。別Sceneで同一Handleが存在しても拒否する。Destroy/Queueもこの入口を共有する。 |
| SceneのIndex Component API | 生存Slotを検証する。Add/Getは無効Indexでout_of_range、TryGetはnullptr、Has/Removeはfalse。Index再利用の世代識別はできないため、保存した参照やScene外の操作にはEntity APIを用いる。 |
| ComponentStorage | Scene内部のIndex専用Sparse Set。Scene所属・世代検証は上位の責務。余分なScene参照や世代配列をStorageに重複保持しない。 |
| ComponentView | Scene/基準Storageへの非所有View。最小Storageを候補に照合し、Snapshotを所有しない。 |
| Editor Command | 対象SceneとHandleを保持し、Scene照合・世代確認後に操作する既存契約を維持。Undo/Redoの再設計はSG1以降。 |

Scene終了はLayerの逆順Detach → Queue破棄 → 生存Entity最終回収 → Storage解放。
通常更新はGame Logic → Animation → 固定Physics → Layer → Destroy Queue Flushを維持する。
Pause時もQueueはFlushされる。60Hz固定Stepとcatch-up上限は変更しない。

## 構造変更と参照無効化

- Component追加・Reserveでvectorが再確保された場合、そのStorageの全Component参照・pointer・Iteratorが無効になる。失敗した追加でも再確保済みなら古い参照は使用しない。
- Removeはswap-and-pop。削除位置と末尾のComponent参照を無効化し、Dense順序を変更する。別Storageの参照はその操作だけでは無効化しない。
- Clear、Entity破棄、Scene終了は対応するComponent参照を無効化する。
- View走査中の対象Storageへの追加・削除、即時Entity破棄、Flushは禁止。終端IndexとDense位置の変化を安全には追従しない。
- View走査中はComponent値の変更とQueueDestroyEntityを使用できる。QueueはEntity全体を保持し、走査終了後にFlushする。構造変更後はView/Iterator/Component参照を再取得する。
- 世代は破棄時に増える。古いQueue項目や二重破棄は生存判定で拒否し、同じIndexを再利用したEntityを破棄しない。親破棄時は子のParentをInvalidEntityHandleへ戻す。

## Storageの例外保証

EmplaceはComponent構築後のEntity配列追加に失敗するとComponentをpopして取り消し、
成功するまでSparse索引を公開しない。Sparseの容量拡張は失敗前後で残ることがある。
再確保時の例外保証はstd::vectorに従う。copy不可かつmove構築が例外を投げる型では
要素値の保証が弱くなるため、copy可能またはnoexcept move構築の型を使用する。

RemoveはComponent移動代入が成功してからEntity/Sparse索引を更新する。
移動代入失敗時も索引対応と要素数を保つが、Component値の巻き戻しは保証しない。
SceneのEntity破棄全体も複数Storageを跨ぐトランザクションではないため、通常のEngine Componentは
noexcept移動代入・破棄を推奨する。Throwing型の例外保証を強制する設計変更は行わない。

テンプレート実装はComponentStorage.inlとScene.inlへ分離。
Sceneの完全型を必要とするEntity/ComponentViewの定義もScene.inlから参照する。
既存の説明コメントと無効コードは移動先にも維持した。

## 検証と残課題

- Storage単体テスト: GCC C++17、Wall/Wextra/Werror、AddressSanitizer/UndefinedBehaviorSanitizerで実行。
- Scene/Entity/SceneLifecycleSelfTests: GCCの構文確認を実施。MSVC向けの既存命名を許容する-fpermissiveと、無効コードのUnicodeメモを許容する-fno-extended-identifiersを使用。
- LeakSanitizerはptrace下で動作しないため無効化。Leak検証は未実施。
- プロジェクト/Filter XML、新規Sceneファイルとstructure.txtの整合を確認。既存baselineスクリプトはCR単独行をメモリ上で正規化して確認（ファイル自体は変更しない）。
- GCC構文確認のWarningは既存の型名とメンバ名の重複による7件。Storage単体テストのコンパイルはWarningなし。
- Windows Debug/Releaseビルド、--test-scene-lifecycle実行、Editor/Physics/Animation実機確認は未実施。
- 実行手順はRoot/Tests/README.md。Windowsでの検証が完了するまでSG0完了とは扱わない。
- Generationの32bit周回とScene pointerのアドレス再利用は今回の変更で解決しない。EntityをScene lifetime外へ持ち出さない既存契約を維持する。

## SG0 Windows検証の自動化

`scripts/test-windows-scene.ps1`を追加し、Windows BuildのDebugジョブから呼び出す。
独立StorageテストをMSVC C++17 /W4 /WX /MT /UNDEBUGでコンパイル・実行した後、
Debugアプリの`--test-scene-lifecycle`を実行する。終了コードと成功メッセージを照合し、
コンパイル結果・Storage実行結果・Scene Lifecycle実行結果を別ログへ保存する。

既存Debug/Releaseビルドは継続し、SceneテストはDebugビルド直後に実行する。
ローカルではWorkflow YAML・Step順序・プロジェクトXML・diff整合を確認。
Windows PowerShell/MSVCを持たないLinux環境のため、Windowsスクリプトの実行確認はCIで行う。
結果が得られるまではWindows検証を合格として扱わない。
