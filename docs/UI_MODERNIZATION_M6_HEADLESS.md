# M6 Headless UI Testing

既存の RavenUITests 起動時に CPU 上で UIDrawList を採取し、Root/Tests/Snapshots の期待値と描画順のまま比較します。画面の読み戻しや GPU 初期化は不要です。

対象はレイアウトと子の Clip、ポインタ押下、フォーカス、Popup とライトテーマ、固定 Atlas の UV と原点です。Atlas のケースは配置済み Quad を対象とし、フォントのラスタライズは既存テストの責務です。

書式は先頭にスキーマ版、以降はコマンドごとに1行です。型・矩形・色・変換・Clip・UV・Texture・頂点列を比較します。数値は固定ロケールで小数点以下4桁に丸め、負のゼロを正規化します。非有限値と未登録 Texture は失敗扱いです。テストで登録した Texture には明示的な名前を使います。

通常のビルドと比較はリポジトリルートから実行します。

~~~powershell
./scripts/test-windows-ui.ps1 -Configuration Debug
./scripts/test-windows-ui.ps1 -Configuration Release
~~~

意図した描画変更のときだけ、リポジトリルートからビルド済みの exe に --update-ui-snapshots を渡します。

~~~powershell
& ./Root/x64/Debug/RavenUITests.exe --update-ui-snapshots
git diff -- Root/Tests/Snapshots
& ./Root/x64/Debug/RavenUITests.exe
~~~

警告や差分を確認した上で期待値をコミットします。CI の通常起動では期待値を更新せず、最初に異なるコマンド番号と項目を表示します。比較精度より小さい変更は検出対象外です。
