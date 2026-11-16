# UI Modernization M3: Inspector View-driven Pull

基準: 6ec2d2f9（M2マージ済み）。M3の対象はInspectorの依存分離と同期/編集競合であり、UIElementへModel/Controllerを持ち込まない。

## 現在の構造と変更理由

既存InspectorRavenPanelは値Snapshotだけを表示するViewだったが、Modelの検証・Preview・Command生成の11個のHandlerと補助関数がEditorLayerに集中していた。
EditorLayerが各FrameでSnapshotを作ってPushし、数値欄のSetValue/CameraのSetRangeは同値でもText/IME/ローカルUndo/確定基準を初期化していた。
M3では既存処理をInspectorControllerへ移し、Viewが必要なFrameに一度だけPullする。

```text
EditorLayer: Active Scene / Selectionの所有元へのProviderを接続、Viewの更新時期を管理
    -> InspectorRavenPanel::Update(): Providerを一度呼び値Snapshotを取得
        -> InspectorController::PullSnapshot(): 現在Scene/Selectionを検証し既存CaptureInspectorSnapshotを使用
    <- InspectorSnapshot: Scene pointerを含まない値 + SelectionCount/SelectionRevision
    -> Input同期: 通常は同値比較、対象切替では編集基準を強制リセット
    -> Preview/Commit Action: Controllerが現在の対象を再検証し、既存Command/Historyを利用
```

旧Update(snapshot)とHandler設定APIはCPUテスト・外部の値供給・Scene解除通知用として維持する。
Controllerは新しいUndo履歴を持たず、Camera/RigidBody/ColliderのApply/比較ヘルパーも既存コードを移動して再利用する。
Transformは既存SetWorldTransformとTransformCommandを使用し、Local/Worldの座標規約を変更しない。
CameraのFOV/Clip単位や既存入力範囲も維持する。

## Lifetimeと対象の同一性

ControllerはScene/Selectionを所有せず、操作ごとにProviderから再取得する。
保存したScene pointerは比較だけに使い、そのpointerを逆参照しない。
Entityのbool変換は所属Sceneを逆参照するため、Scene一致確認より先に使わない。
Actionは表示済みの単一選択、現在のScene、Handle/Generation、現在の選択、履歴Epoch、生存/Component条件を再検証する。
選択変更/Entity破棄/Scene交換が次のPullより先に起きても、旧対象への要求を拒否する。

EditorLayerのSetSelectedEntity/OnActiveSceneChangingはControllerの表示対象を即時失効させる。
Scene変更前通知は旧Scene破棄前に必要。Application/EditorLayerの既存契約を維持し、同じアドレス/Handle値が再利用されても選択を引き継がない。
SelectionRevisionはControllerの表示対象変更を値で表し、同じHandleの新しいSceneや履歴再設定でもViewのSessionを区別する。
世代枯渇時はwrapして古いSessionと混同せず、以後の表示/編集を拒否する。

EditorLayerはControllerをViewより先に宣言し、逆順破棄でViewが先に終わるようにした。
Controllerはcopy/moveを禁止し、thisを捕捉するCallbackの接続先アドレスを固定する。
終了順はView.Detach（Session解除→Widget解除）→Controller.Unbind（Callback/Provider解除）→Scene履歴解除。
Providerが捕捉するApplication/Selection所有元もControllerより長生きする。
ControllerはView/Widgetのpointerを保持しない。Bind/Unbind時はViewを空Snapshotへ同期し、別Controllerの同じRevision/Handleでも旧Beforeを引き継がない。

## 同期と編集中の競合

UIInputNumberへSetValueIfChanged/SetRangeIfChangedを追加し、新しい処理本体はUIInputNumber.cppへ分離した。
通常同期はClamp後の値と確定済み基準がともに同じなら更新せず、Text/Caret/IME/ローカルUndoを保持する。
既存SetValue/SetRangeは明示的な初期化・表示正規化の意味を維持する。
対象切替では同じ数値でも旧Text/Commit基準が別物なのでSetValueを強制する。

同一対象でFocus中の数値欄はSnapshot同期を抑止する。ほかの欄には現在Model値を反映する。
Focusは生成時のContextでなく、現在Rootを所有するContextから取得するため、補助Window移譲後も同じ規則が働く。
対象の有効性/Revision/Count/Handle変更とComponentの有無変更では、古い編集Sessionを同期より先に解除する。
Section非表示から発生するFocus Lostも同期ガードの内側に置き、古いTextによる新しいModelへのPreview/Commitを防ぐ。

PreviewはRuntimeへ即時反映し、Undo履歴には積まない。
連続Previewの最初のBeforeを保持し、Enter/Focus喪失などのCommitで現在ModelをAfterとして1件だけ記録する。
Afterに古い表示Snapshotを使わず、Preview後の外部更新をCommit時に巻き戻さない。
既存TransformCommandはTransform全体を1単位としてUndoする。フィールド単位の競合解決へ変更していない。
数値Stepは従来どおりStepごとの確定単位。GizmoのDrag終了時のCommand統合・共有Historyも維持する。
新しいドラッグWidgetやUndo基盤は追加しない。

## 複数選択

既存Editorの選択は単一Entityで、複数編集UIは追加しない。
InspectorSelectionはPrimary + Countを持ち、Count>1を明示的に非編集対象として扱う。
複数選択要求が来た場合は主要Component値を公開せず、Viewは非対応状態を表示し、ActionもPull前から拒否する。
単一選択へ戻った場合はRevisionを変えて入力基準を再同期する。

## 検証

- 比較同期で同値の非正規化Text/Caret/ローカルUndoを保持し、値/範囲の変更は反映する。
- ViewはUpdateあたり1回だけPull。同値60回のPullでTransform/Camera入力TextをMeasure Dirtyに戻さない。
- 複数Preview→Enter重複→Focus喪失でUndoが1件。Model外部更新、未確定の1e-、他欄のPull同期。
- Context移譲後のFocus判定、Undo/Redo、選択切替時の古い入力拒否。
- Camera/RigidBody/Collider/Primary Cameraの実Command Undo、Camera不正値・Static Body MassのSafety Check。
- Component削除時のFocus喪失、再追加時の確定基準、Entity破棄/Generation更新、複数選択拒否。
- 同じSceneアドレスとHandle値を意図的に再利用し、旧入力Sessionが新SceneへCommitされないこと。
- 主要Camera/RigidBody/Collider数値欄の実CallbackをStep→Preview→Commit→Undoで検証する。
- 異なるControllerへ同じViewを再接続し、同じRevision/Handleでも旧入力Sessionを引き継がない。
- 既存EditorLayer Scene寿命回帰をUIテストから呼び、実際のLayer通知/選択解除経路も検証する。
- 既存InspectorSnapshot自己テストはassertを使ったAttachを含むためDebugで実行。新M3のCheckベース回帰はReleaseでも実行する。

## 結果

2026-11-17実行。Windows x64 / MSVC 14.44.35207。

| 確認 | 結果 |
|---|---|
| Debug UIテストターゲットビルド | 成功・終了コード0 |
| Release UIテストターゲットビルド | 成功・終了コード0 |
| Debug UI回帰全体 | 成功・終了コード0。新M3回帰、EditorLayer Scene寿命、既存Snapshot自己テスト、M0〜M2回帰を含む |
| Release UI回帰全体 | 成功・終了コード0。新M3回帰とEditorLayer Scene寿命はassertに依存せず実行。既存assert専用Snapshot自己テストは除外 |
| Debug OpenGL GPU統合ビルド/実行 | 成功・終了コード0。Font Atlas/Texture転送、共有Context描画/Resize/復帰、Cache off/onを確認 |
| 新規3ファイルの構成 | 実ファイル、structure.txt、プロジェクトglob、Filter登録を静的検証 |
| 空白差分 | git diff --check成功 |
| 通常Editorの専用ターゲット/実起動・手操作 | 未実施。UIテストターゲットは実EditorLayerとControllerを含むEngine全体をコンパイル/リンクする |
| 実InspectorのマウスDrag/IME実機操作・異DPI・DX12/Vulkan | 未実施。GPU基盤成功をInspector全操作の実機確認とは扱わない |

同値60回のPullで入力TextをMeasure Dirtyへ戻さず、1 UpdateでSnapshot取得が1回であることを確認した。
時間・Allocation改善の定量測定はM3では行っていない。M0/M2の計測ケースも同じ回帰exeで動くが、その出力をM3固有の性能結果としては扱わない。

既存C4244（UI回帰のfloat変換）とDebugリンクのLNK4098は継続。
今回追加した箇所に対するコンパイラWarningは確認されなかった。
M0からのホスト固有回避（GLFW 3.4 Native Headerの追加include、RavenGenerateLinkDebugInformation=false）を使用し、依存ファイル/プロジェクトのビルド設定は変更していない。
最終検証後のソース変更はEditorLayer/Panelの責務に関するコメント更新だけ。

## 再実行

```powershell
$env:CL = '/ID:\Engine\artifacts\sg3-deps'
msbuild Root/Root.sln /m /p:Configuration=Debug /p:Platform=x64 /p:RavenUITest=true /p:RavenGenerateLinkDebugInformation=false
# Root/Rootを作業Directoryとして起動。Releaseは構成/Pathを変更する。
& D:/Engine/Root/x64/Debug/RavenUITests.exe *> D:/Engine/ui-m3-debug-tests.log
```

GUI表示可能な環境でGPU基盤も確認する場合はRavenUITestの代わりにRavenUIGPUTest=trueでビルドし、
RAVEN_UI_TEST_FONT='C:/Windows/Fonts/arial.ttf'でRavenUIGPUTests.exeをRoot/Rootから起動する。
ローカルログはui-m3-debug-final-build.log / ui-m3-release-final-build.log / ui-m3-gpu-build.log / ui-m3-*-tests.log。

## 構成と残課題

新規ファイルはEditor/Panels/InspectorController.h/.cpp、UI/Widgets/UIInputNumber.cppの3つ。
既存Root.vcxprojのRaven/**/*.h/.cpp globで登録されることを確認し、.vcxproj.filtersへ明示登録した。
WidgetsのFilterを追加し、対応する既存UIInputNumber.hも同じ論理Folderへ登録した。structure.txtも実構成に合わせて更新した。

既存UIInputNumberの他のinline実装とInspectorEditCommandのテンプレート実装は今回一括整理していない。
既存Snapshot自己テストのassert内副作用も今回の修正へ混ぜず、Debugのみ実行する。
単一選択の編集を維持しており、複数選択の値混在表示/一括編集、非同期Action、フィールド単位の外部更新競合解決は対象外。
次はM4のShared Definition/Instance Stateを計測に基づいて検討する。M4は別承認とし、自動的には開始しない。
