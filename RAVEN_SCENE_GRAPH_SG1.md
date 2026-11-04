# Scene Graph SG1 — 階層管理の責務分離

SG0統合後のmaster `0f75b47f3b03ff15d8eb839a7994b2ddde33f306`を基準とする。
SG1の実装・テスト・計測を追加。Windowsでのビルド・実行と計測結果はCIで確認する。
SG2以降には進まない。

## 責務と所有権

- SceneがEntity SlotとRelationshipComponentを所有する。Parentの正規データはComponentだけとする。
- SceneGraphは状態を持たない操作クラス。Sceneを所有せず、Scene参照を保存しない。
- 既存Scene::CanSetEntityParent/SetEntityParent/GetEntityParentはSceneGraphへ委譲する互換窓口。
- Scene::DetachEntity/GetEntityChildren/GetEntityAncestors/GetEntityDescendantsを追加する。
- SceneGraphだけがSlot/Storageを読み取り、階層の妥当性と親破棄時の処理をまとめる。
- EntityHandleはScene内識別子であり、Handle APIの呼び出し側がScene所属を保証する。
- Sceneの終了・固定Physics Step・Animation更新順序・Renderer内部は変更しない。

## 階層操作の契約

Reparentは生存した子・親だけを受け付け、自己参照・循環・壊れた祖先への接続を拒否する。
InvalidEntityHandleへの変更はDetach。RelationshipComponentが無いEntityも従来どおり親設定で追加する。
親破棄時は直下の子をRootへ戻し、孫は存続中の子を親とする関係を保つ。

Transformは従来のWorld-space TRS。親変更・Detach・親破棄・Undo/RedoでTransformを書き換えず、
Matrix分解、Local/World変換、物理姿勢の再同期を行わない。これらの変更はSG2以降。

Childrenは直下だけ、Ancestorsは近い親からRoot順、Descendantsは非再帰の親→子順（幅優先）。
返却VectorはHandleのSnapshotで、Component参照やScene lifetimeを延長しない。
兄弟順序はStorageのswap-and-pop等で変わるため保証しない。root自身は子/子孫に含めない。
無効な起点は空を返す。Ancestorsは循環検出時に空、Descendantsは重複を除き一度だけ訪問する。
公開RelationshipComponentを直接破損させても走査は停止するが、通常の変更はScene APIを使用する。

## Editorとの接続

DeleteEntityCommandの子Snapshot取得をGetEntityChildrenへ統一した。
Undoは空のRelationshipからSetEntityParentで復元し、失われた親や循環する親へ接続しない。
削除中に別の親へ移動した子は奪い返さず、まだRootで生存する子だけを再接続する。
外部SceneのEntityを渡したDelete Commandは拒否する。

Reparent Commandの公開契約は維持。Delete Undoで再生成された親Handleを履歴へremapし、
続くReparentのUndo/Redoも新世代を参照することを検証する。
Delete Command本体を.h/.cppへ、Capture/Restoreテンプレート定義を.inlへ分離した。

SceneHierarchyのRuntime SnapshotはSceneの互換窓口から親を取得する。
Sceneを持たない純粋Snapshot入力の循環・孤児正規化はUI側の防御として維持する。
削除された選択Entityを新世代へ暗黙に置き換えず、有効な選択Handleだけを表示する。

## 計測とキャッシュの判断

ChildrenはRelationship Storageの都度走査（O(R)）。単発の親削除やSnapshot取得で使う。
Descendantsは呼び出し内で一時的な子リンクを作り、O(R + V)で訪問する。
NodeごとにChildrenを再走査するO(R × V)方式と、wide/chain各1024 Entity・3反復で比較する。
計測には一時索引の構築・返却Vectorの確保を含め、訪問数の一致を必ず検証する。
時間の閾値はテスト合否に使わない。

常設の子リンク/キャッシュは追加しない。公開Componentの直接編集・Undo/Redo・世代更新ごとの
失効通知が必要となり、現在の利用経路に対する維持コストの優位性は確認していないため。
Debug/Releaseの計測は`Root.exe --measure-scene-graph`で再現できる。
Windows CIは両構成の計測ログを`artifacts/logs/windows-scene-*-results.log`へ保存する。

## 検証

- SceneGraph SelfTest: Reparent/Detach、循環拒否、破損データ、古いHandle、親破棄、深さ1024の非再帰走査。
- World-space行列・Camera FOV・RigidBody入力・Collider設定が階層変更で変わらないことを確認。
- Editor SelfTest: Reparent履歴、親削除と複数Undo/Redo、Handle remap、選択・Hierarchy Snapshot、別親へ移った子の保護。
- Windows Debug CIでStorage/Scene Lifecycle/SceneGraph/Editor SelfTestと走査計測を実行。
- Release CIでもEngineビルドと走査計測を実行。既存UI回帰テストは継続。
- Editor/Physics/Animationの実機描画・シミュレーション回帰はこのLinux環境では未検証。
