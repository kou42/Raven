# Phase 9 編集DocumentとEditor連携（2026-11-13）

## 責務とデータの流れ

`DataDocument`は引き続き不変です。Parser、既存Deserialize、既存Asset形式は変更しません。
`MutableDataDocument::Reset(DataElement)`が入力を所有する編集Storageへコピーします。
StringはDecoded UTF-8、Binaryは任意Byte、数値は既存の型とPayloadのbit値で保持します。
JSONの空白・Escape表記・数値の字句表現は保持しません。文字列を二重Decodeしません。

編集は`InsertArray / InsertObject / Remove / Replace`で行います。値の入力は不変Elementです。
既存の編集部分木を複製する場合は、その部分木を先にFreezeしてください。循環や二重所有は作れません。
ObjectのキーはDecoded文字列で比較し、NULも保持します。重複キーと不正UTF-8は拒否します。
`TryGetChild`はArray要素またはObjectの挿入順の値を返します。キー検索は`TryFind`を使います。
Rootの削除は拒否しますが、Root全体の置換は可能です。

成功した`Freeze`は独立した所有Documentを作ります。型取得・Traversal・数値変換は
既存DataElementへ委譲できるため、編集側に数値変換の別実装を追加しません。
Freeze後は既存DataReader / JSON Writer / MessagePack Writerへ渡せます。
BinaryをJSONに暗黙変換しない規約も維持します。File I/OはData層に追加しません。
専用AssetではFreeze→型検証→既存SaveTestAsset→LoadTestAssetで保存・復元します。
この型付き保存経路の未知フィールド破棄は既存スキーマの規約です。汎用Freeze自体は未知メンバーも保持します。

## Handle・View・Thread

- HandleはDocument固有Owner ID、Epoch、Slot index、Generationを持ちます。
  Owner IDは同じアドレスへのDocument再構築でも再利用しません。
- 通常編集では変更部分木以外のHandleを維持します。削除・置換した部分木は無効です。
  Free ListからSlotを再利用してもGenerationが異なります。世代上限のSlotは永久退役します。
- Reset・Compact・Undo/Redo復元はEpochを進め、全編集Handleを無効にします。
  Undoで値が戻っても削除前Handleは復活しません。呼び出し側でRootから再取得してください。
- 成功した変更では、変更対象以外のString Viewもすべて無効です。
  失敗時は状態、Revision、Handle、View、Try API出力を維持します。
- Handleの検査には生存中のDocumentが必要です。破棄済みDocumentのメソッドは呼べません。
- 編集DocumentとEditor履歴は単一Thread専用です。Freezeも編集と競合させません。
  Main ThreadでFreezeした独立DocumentだけをWorkerへ渡せます。
  事前FreezeしたDocumentをWorkerが読む間に、元の編集Documentを変更するテストを実行しました。

## Storage・上限・失敗時の保証

既存HandlePoolはSlotごとの安定アドレスを持つ非Copy型です。今回は検証後Commitを行うため、
同じPoolHandle / Generation / 上限世代の退役規約を用いた連続Slot Storageを追加しました。
編集ごとに候補Storageをコピーし、変更・Tape生成・既存Document検証が全成功後にswapします。
CompactはFreeze→新しい密なStorageへのResetで空きSlotと不要領域を除きます。

入力時はString / Binary / キーの値長、合計Byte、キーを含むToken数、コンテナ要素数を
追加確保前に確認します。Import時はEscape文字列のRaw長にも上限を適用します。
そのためDecoded値が短くてもRaw表記が編集Limitを超える入力は拒否する保守的な契約です。
編集後の親との合計深さも検証します。再帰処理のStackを制限するため、編集用の深さは
`min(DataLimits.MaxDepth, MutableDataMaxDepth=128)`です。不変Parserの最大深さ設定とは独立です。
FreezeのOffsetは生成したSource内の位置で、元JSONの位置ではありません。

初期版は編集1回あたり全Storageのコピーと全体検証を行い、変更量に対して定数時間ではありません。
Undo/Redoも前後の不変Snapshotと照合用の符号化結果を所有するため、保持メモリが増えます。
既存履歴の128件制限を使い、Byte数による履歴予算や差分Commandはまだ追加していません。
大きな実運用Assetへ導入する前に、これらの時間・Peak Memoryを別途測定してください。

## Editor Command

編集Sessionの所有者が`shared_ptr<MutableDataDocument>`を保持し、
`DataDocumentEditCommand`はweak_ptrだけを保持します。SceneやSessionの寿命を延ばしません。

1. 現在のSceneを既存`SetEditorCommandHistoryScene`で設定します。
2. 現在のDocumentをFreezeし、別の編集用draftで変更して、編集後SnapshotをFreezeします。
3. `DataDocumentEditCommand::Create(scene, target, editedRoot)`で前後Snapshotを確定します。
4. `ExecuteAndRecordEditorCommand`へ渡します。反映後に同じCommandを登録するAPIは使いません。
5. 既存Undo / RedoからSnapshotを復元します。保存は必要な時点でFreezeして呼び出し側が行います。

Commandは異なるScene、期限切れSession、履歴外の変更、意味が変わらない編集を拒否します。
照合は決定的MessagePackの既存規約に従い、Object順序と非負整数の符号分類を同値として扱います。
単純なRevision照合では後続CommandをUndoした後に以前のCommandをUndoできないため、内容を照合します。

Scene交換・明示的Clearで履歴Epochを進め、履歴外で保持した古いCommandも拒否します。
Scene破棄前に`SetEditorCommandHistoryScene(nullptr)`等の既存通知が必要です。
この通知とEpochを使い、同じSceneアドレスが再利用されても旧Commandを適用しないテストを行いました。

履歴の移動先容量はCommand変更前に確保します。この確保に失敗した場合は対象と両Stackを維持します。
Commandの一時的なOutOfMemoryは`RetainAfterFailure`で履歴に残し、Undo/Redoを再試行できます。
対象消失や内容不一致による無効Commandを破棄する既存の動作は維持します。
既存Commandはこの追加契約を既定falseとするため、従来の無効Command処理を変えません。

## 検証

```powershell
./scripts/test-windows-structured-data.ps1
./scripts/test-windows-structured-data.ps1 -AllocationTests
# アプリの増分ビルド後に、Windowを作らずEditor / Scene回帰を実行する
./scripts/test-windows-scene.ps1 -SkipBuild -SkipBootstrap
```

- MSVC C++17 / W4: 既存5,440＋新規65＝5,505チェック成功、警告0。
- 確保失敗注入: MutableDataDocumentは610チェック成功。Reset / Insert / Remove / Replace /
  Compact / Freezeの確保点で元状態・出力維持を確認。ParallelDeserializeも50チェック成功。
- Editor Commandは50チェック成功。2操作のUndo/Redo、Redo分岐破棄、無効Scene / 期限切れSession、
  外部編集の拒否、同じSceneアドレスの再利用、Undo後の古いHandle拒否、一時失敗時の履歴保持・再試行を確認。
  Undo/Redo後の専用Assetを実ファイルへ保存して再読み込みした。
- 既存Scene / Editor回帰テスト群（Storage、Transform Math、Scene lifetime / lifecycle /
  transforms / runtime / view / simulation、Physics collision、Graph、Entity Command、Graph計測）も成功。
- アプリDebug増分ビルドは既存の`/p:RavenGenerateLinkDebugInformation=false`でリンク成功。
  初回リンクの既存LNK4098を確認。通常PDB生成の既知MSPDB140.DLL不一致は修正していない。
- 構成とFilters / Project XML、差分を確認した。新規の汎用編集UIと実運用Asset移行は対象外。

Editor実行入口は`Root.exe --test-data-document-edit`。結果は
`artifacts/logs/windows-scene-data-document-edit-results.log`へ保存されます。
Dataの結果とビルドログは`artifacts/structured-data/`です。
Editorの一時失敗契約は専用Commandでテストし、低レベル編集のOutOfMemoryは確保注入で検証しています。
Editor Command自体・履歴reserveの各確保点への注入、Generation/Epochの実際の上限到達、
Sanitizer / ThreadSanitizer、GUI操作、他Compilerの実行は未検証です。
Phase 10以降には自動で進まず、次は実用途を限定した編集Sessionへの接続と履歴メモリ予算の検討を推奨します。
