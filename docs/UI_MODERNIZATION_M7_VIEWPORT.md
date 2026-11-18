# M7 TreeView / Table の可視範囲

TreeViewは1回の描画内で、展開済みノード順序・スクロール位置・Scrollbar寸法を共有します。描画ループはスクロール位置から該当する行へ直接移動します。Drop先のHit Testも行ごとに繰り返しません。

展開済みノードの平坦化は毎描画で行うため、全体の計算量には展開済みノード数に比例する処理が残ります。公開されたNodeの変更を取りこぼさないよう、順序をフレーム間で保持していません。Tableは既存の可視行区間を使います。

CPU回帰に1,000／10,000行の合成データを追加しました。TreeViewの末尾選択・スクロール・折り畳み後の選択と描画コマンド数、およびTableが可視行のセルだけを取得することを確認します。実Sceneの構築時間や操作時間はこのテストの測定対象外です。


## SceneからHierarchyまでの計測

Release x64でSceneに親1体と子を生成し、CaptureSceneHierarchySnapshot → SceneHierarchyRavenPanel.Update → UIContext.EndFrameを実行しました。操作区間は1回、描画区間はウォームアップ1回後の30回平均です。時間の単位はmsです。GPUとFont Atlasを使わないため、文字描画を含むフレーム時間ではありません。

| Entity数 | Snapshot | 初回構築 | CPUフレーム | 選択・スクロール | 折り畳み | 展開 | 描画コマンド | Table取得行 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 0.455 | 2.962 | 0.010 | 0.159 | 0.041 | 0.024 | 9 | 3 |
| 10,000 | 4.678 | 189.607 | 0.233 | 2.322 | 0.489 | 0.224 | 9 | 3 |

リポジトリルートからReleaseのUIテストexeに --measure-ui-scene-hierarchy を渡すと再実行できます。同じScene SnapshotをTableのデータソースへ接続し、末尾表示で可視3行だけを取得することも確認しました。実Entityの親子関係、末尾選択へのスクロール、折り畳みでの親への選択復帰も検査します。初回構築には繰り返しのノード検索があり、大規模Sceneの初回表示時間は課題として残ります。この計測は単一の親に子が並ぶ構成であり、深い階層との比較や変更前後の速度比較は行っていません。

## EditorとRuntimeの共通化

InspectorのTransform・Camera・RigidBody・Colliderで数値プロパティ行のラベル、寸法、入力欄の構築を共有しました。値域と編集コールバックは各Sectionで指定するため、既存ControllerのPreview、Commit、Undo境界を保ちます。Hierarchyは既存TreeViewとSnapshot境界を再利用しています。

ApplicationはメインWindowにフォーカスがあり、Scene遷移による入力ブロックがない間だけ、標準Gamepad 0を最前面Screenへ渡します。Title・Pause・Settingsは共通UIScreen処理へメニューのパスを登録します。縦Stickの新しい入力でメニュー内のフォーカスを移動し、Aの押下でUIButtonの既存Enter処理を使います。初回サンプル、再接続、別Screenからの復帰では保持中の入力を再発火しません。実機コントローラーは未検証です。

CPUテストでHUDとTitle Menuを同じUINavigationManagerへ接続し、UIAnimationBindingでボタンのOpacityを変え、そのUIContextがUIRendererへフレームを渡すことを確認します。Gamepad状態はテストから注入し、既存アクションの呼出回数と、Settingsを開く／閉じる際の押しっぱなし入力の抑止、PauseのResumeとSettingsのBackを検証します。測定用Rendererを使うため、このテストは実GPUのTitle/HUD描画を検証するものではありません。

## Graph / Timelineの設計検討

共有候補は、論理座標の表示区間、パンとズーム、Clip、ポインタの逆変換、安定IDによる選択です。意味の異なるデータはそれぞれのモデルに残します。Graphはノード・ポート・接続、Timelineは時刻・Track・Keyを管理し、Widgetが編集要求をControllerへ渡します。描画は既存UIDrawListへ出力し、まずDiagnosticsのMeasurementTimelineとAnimationの時刻表示で共有境界を試せます。接続編集とキー操作を同じWidgetへ押し込む設計は、Hit TestやUndoの粒度が異なるため対象外です。

## Dear ImGuiの完全撤去

Dear ImGuiは[脱ImGui Phase 8](../roadmap/close/RAVEN_IMGUI_PHASE7_VALIDATION.md#phase-8-dear-imgui依存の撤去2026-11-12)で製品から撤去済みです。M7の要件は、EditorとRuntimeのUIをRaven UIで賄い、この撤去状態を維持することです。

既存の依存計測スクリプトで、依存ファイル、API呼び出し、Visual Studio設定の参照、実行時切替を確認し、すべて0件でした。追跡中のファイル名とCI／ビルド設定も確認し、製品に同梱されるDear ImGuiソースや設定参照は見つかりませんでした。移行履歴と監査用スクリプトは検証記録として残しています。

既存Ravenパネルの選択・名前変更・プロパティ編集とUndoはCPU回帰で確認済みです。実機コントローラーおよび実GPUによるRuntime操作の検証は別項目として残ります。
