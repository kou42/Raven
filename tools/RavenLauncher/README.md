# Raven Launcher

Raven の Git 更新、Visual Studio プロジェクト同期、ビルド、VS2026 起動を行う Windows 用ランチャーです。

## ビルドと初回設定

1. Visual Studio 2026 Installer で C++ デスクトップ開発、Raven が指定する MSVC v143 ツールセット、Windows SDK を用意します。Git for Windows の `git.exe` を PATH に登録します。
2. VS2026 で `RavenLauncher.vcxproj` を開いて x64 の Debug または Release をビルドします。実行ファイルはプロジェクト配下の `out/x64/Debug/RavenLauncher.exe` または `out/x64/Release/RavenLauncher.exe` です。
3. `Setup...` で Raven のチェックアウトルートを選びます。既定のソリューションは `Root/Root.sln` です。別のソリューションを使う場合はリポジトリ相対パスで入力し、`Load Configs` で構成を再読み込みします。
4. `Diagnose` で Git、ソリューション、Visual Studio 2026、MSBuild を確認します。Raven の既定ソリューションでは、選択構成に必要な MSVC と Windows SDK も確認します。診断はビルドや Git の更新を行わず、結果を画面とログに表示します。

## 更新して開く

`Update & Open` を選ぶと、次の順番で進みます。

1. **Git 更新** — クリーンな追跡ブランチを確認して fetch します。更新がある場合は、表示されたコミットの fast-forward を承認します。
2. **プロジェクト同期** — 差分をログで確認します。候補があれば `Review & Continue...` を選んで承認します。手動確認項目はログに残ります。
3. **ビルド** — 選択した構成で MSBuild を実行します。エラーと警告は実行中の画面とビルドログで確認できます。
4. **Visual Studio 起動** — ビルド成功後に選択したソリューションを開きます。

`Git Update`、`Sync Projects`、`Build`、`Open Visual Studio` は個別に実行できます。`Cancel` や確認ダイアログの `いいえ` で停止できます。Git または XML の反映中は書き込みの完了を待ちます。停止時は、完了済みの工程数がログに表示されます。

`Retry Update & Open` は現在の状態を Git から確認し直します。承認して反映済みの Git 更新や同期は残ります。同期後のビルドに失敗し、プロジェクトファイルの変更が残っている場合は、修正後に `Build` と `Open Visual Studio` を個別に使えます。

## ログとトラブルシューティング

設定とログは `%LOCALAPPDATA%\RavenLauncher\` にあります。

| ファイル・フォルダー | 内容 |
| --- | --- |
| `config.json` | チェックアウト、ソリューション、選択構成 |
| `launcher.log` | 工程の結果、停止理由、同期のバックアップ位置 |
| `build-logs\` | ビルドごとの標準出力・標準エラー、終了結果 |
| `sync-backups\` | 同期前の元の XML。実行ごとに別フォルダーに保存 |

- **Git 更新が止まる** — `git status` と追跡設定を確認します。ローカル変更、分岐、追跡先なしの場合は Git 側で状態を解消してから再試行します。接続や認証で止まる場合は、普段使っている Git クライアントでリモートへのアクセスを確認します。
- **同期差分を反映できない** — Visual Studio などによる編集中の変更を保存し、プレビューを取り直します。バックアップから戻す場合は、ログに出た実行別フォルダーを開き、戻したい XML の現在との差分を確認してから、同名の元ファイルをチェックアウトの `Root/Root/` にコピーします。
- **MSBuild、MSVC または SDK が見つからない** — `Diagnose` またはビルドログで不足しているコンポーネントを確認し、Visual Studio Installer で追加します。Raven の依存ファイルの準備には、リポジトリの `scripts/bootstrap-windows-build.ps1` を利用できます。
- **VS2026 が開かない** — `Diagnose` で IDE の検出とソリューションのパスを確認します。`Open Visual Studio` で起動工程だけを再試行できます。
