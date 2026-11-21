# Raven プロジェクト整理 M5 Audio

`AudioService.cpp` と `AudioService.h` を `Raven.Audio` 静的ライブラリの所有一覧に登録した。両ファイルは既存のパスにあり、`Raven.Runtime → Raven.Audio → Raven.Foundation` の方向に参照する。Rootがリンク時にAudioのアーカイブを解決する。

ApplicationがAudioServiceを所有し、フレームごとに更新して、終了時に停止する。AudioServiceはインストールされたバックエンドを所有する。シーンにサービスの寿命を結び付けず、シーンを切り替えてもバックエンドを保持できる。今回の切り出しで、既存のバックエンド契約を使ってウィンドウ・GPU・Runtimeから独立した検証が可能になった。

## 構成と検証

9プロジェクトの参照グラフに循環がないことを確認した。MSBuildの14構成で、全プロジェクトのコンパイル対象はM0と一致し、ソースの二重所属はなかった。Audioのプロジェクト参照とRaven内のinclude依存はいずれもFoundationに向く。

x64 Debug／ReleaseのAudio単独ビルド後、テスト用バックエンドを持つ小さな実行ファイルを `Raven.Audio.lib` でリンクし、各構成で12チェックに成功した。

- バックエンドの開始と公開、フレーム更新、負の経過時間の0への補正。
- 差し替え時に旧バックエンドを停止・破棄してから新バックエンドを開始。
- 開始失敗時の停止と破棄、失敗後の更新抑止、空のバックエンドへの差し替え。
- 明示停止を繰り返しても停止は1回。サービス破棄時も停止してからバックエンドを破棄。

Rootもx64 Debug／Releaseでビルドし、引数なしで通常ウィンドウの表示と正常終了を確認した。両構成でEditorのシーン寿命、データ編集、Scene Runtimeの既存CLIも成功した。入力変更なしの再ビルドではRootとAudioの生成物を再利用した。Win32とUI・GPU・計測構成はMSBuildでの静的評価まで実施した。

単独の検証コードと生成物は `artifacts/pm5/`、ログは `artifacts/logs/project-org-m5-*` にある。ビルドにはM0と同じMSPDB回避設定を使った。実オーディオデバイスでの再生は検証対象外。
