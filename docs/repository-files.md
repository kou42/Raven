# Gitで共有するファイルとローカル生成物

Gitには、別の環境で開発・検証を再現するためのコード、プロジェクト設定、
アセット、テスト入力、スクリプト、設計資料、検証結果の説明を保存する。
CSVは拡張子だけで一括除外せず、用途と保存場所で区別する。

| 保存先・ファイル | 管理方針 |
| --- | --- |
| `Root/Root/Raven/` のコード・アセット・テスト入力 | Gitで共有 |
| `.sln`、`.vcxproj`、`.vcxproj.filters`、CI設定 | Gitで共有 |
| `scripts/` の再利用可能なビルド・検証ツール | Gitで共有 |
| `docs/` とルートの設計・ロードマップ・検証資料 | Gitで共有 |
| `artifacts/` のログ・生CSV・終了コード・PID・配置の退避・作業用スクリプト | ローカルのみ |
| `RavenUI-recording-*.csv` | 自動生成される計測記録のためローカルのみ |
| `RavenEditorDock.layout.json`、`RavenUIDockDemo.layout.json` とバックアップ | 個人の画面配置のためローカルのみ |
| `.vcxproj.user`、`Root/Root/imgui.ini`、ルートの `Root.code-workspace` | 個人用設定としてローカルのみ |
| ビルド出力、`*.log`、`*.err`、`*.bak`、一時ファイル、`.codex-worktrees/` | ローカルのみ |

`artifacts/logs/` は既存のビルド・テストスクリプトの出力先として維持する。
Gitの除外設定はファイルを削除せず、GitHub ActionsのArtifactアップロードも妨げない。
資料中の `artifacts/` への参照はローカル採取物を指し、cloneだけでは復元されない。

計測結果を共有するときは、採取条件・結果・制限を資料にまとめる。
再現に必要な固定CSVや期待値は、個人情報・環境依存情報を確認して
`docs/` または対応するテストの入力データとして保存する。
再利用するスクリプトは `scripts/` へ整備し、`artifacts/` の作業用コピーとは分ける。
既存のローカル生成物は移動・削除せず、そのまま参照できる状態を保つ。

確認には `git status --short` と `git check-ignore -v <パス>` を使用する。
すでに追跡済みのファイルは `.gitignore` だけでは除外されないため、
対象を確認して `git rm --cached -- <パス>` で管理から外す。実ファイルは残る。
