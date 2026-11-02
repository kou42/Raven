# Raven Editor Dock Layout契約

## 保存形式とVersion

Raven UIのDock配置は`RavenDockSnapshot` JSONとして保存し、形式Versionは`kUIDockSnapshotVersion`で一元管理する。現在のVersionは1で、未知のVersionは部分的に解釈せず拒否する。Node IDとTab IDはJSON numberではなく文字列として保存し、64bit整数精度を維持する。

Editorの既定ファイル名は`RavenEditorDock.layout.json`とする。Dear ImGuiの`imgui.ini`は読み込まず、自動変換もしない。両BackendのLayoutモデルとPanel Lifetimeが異なるため、移行時に不完全な対応関係を推測しない。

## 既定配置

- 左20%: Scene Hierarchy
- 中央上: Scene View / Game View
- 中央下: Statistics / Animation Debug
- 右側: Inspector

`EditorDockTabId`をContent Factoryとの安定した契約として使用する。GPU ResourceやPanel ObjectそのものはSnapshotへ保存せず、復元時にTab IDから再生成する。

## 読み込みとFallback

`LoadEditorDockLayoutOrDefault()`は次の順で有効な配置を選ぶ。

1. Raven EditorのPrimary Snapshot
2. `.bak`の直前Snapshot
3. コードで定義した既定配置

PrimaryとBackupは、JSON構文、Version、Dock Tree、Tab参照、選択状態を全て検証してから採用する。Primaryが破損してもBackupを試し、両方が不正なら既定配置へ戻す。読み込み失敗で部分的なTreeを残さない。

初回起動でRaven専用ファイルが存在しない場合は`DefaultFirstRun`、破損や空Pathから復旧した場合は`DefaultRecovery`を返す。呼び出し側はこの結果を診断表示に利用できるが、起動自体は継続する。

## 保存境界

Dock操作中ではなく、Editor shellの安全なFrame境界または終了処理で`SaveDockSnapshot()`を呼ぶ。保存は一時ファイルへ書き込み、既存PrimaryをBackupへ退避してから交換する。Panel Contentの生成・破棄と同じFrame中にSnapshotを差し替えない。

## 補助Windowを開いたままの終了

Editor終了時は`BuildEditorDockLayoutForSave()`でMain DockのSnapshotへ切り離しTabの値情報を合成する。次回起動では元Dockへ戻し、OS補助Windowの位置やサイズは復元しない。JSON形式はVersion 1を維持する。

Applicationは切り離す直前のPane ID、Tab位置、選択ID、切り離し順を保持する。保存時は現在も補助Rootが所有するContentと、Close後に元Dockへの復帰を待つ所有Contentだけを対象とする。WidgetやContextの所有権は移動せず、Window/GPUの終了順も変更しない。

同じPaneからの複数Tabは切り離しの逆順に元位置へ挿入する。現在のDockに残るTabの相対順序と有効な選択を優先し、空Paneでは保存した選択を戻す。既に復帰済みのTab IDは重複登録しない。非表示Panelは切り離し対象に存在しない限り追加しない。

元Paneが存在しない場合は既定配置のPaneが生存していればそこへ、なければ現在のTreeの最初のTab Paneへ戻す。保存のためにSplit構成は作り直さない。不正なID、重複した現在のTab、無効なSnapshotを検出した場合は保存を中止して診断を出し、既存ファイルを維持する。
