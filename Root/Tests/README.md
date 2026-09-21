# UI Reflow回帰テストの実行

`Root/Tests/UITextReflowTests.cpp` は通常のアプリケーションとは別の `main()` を持ちます。
既存の `Root/Root/Root.vcxproj` に `RavenUITest=true` を指定すると、通常の `main.cpp` を除外してテストをエントリーポイントに切り替えます。
UI Coreを含む既存エンジンのリンク設定を再利用するため、テスト専用に依存ライブラリを複製しません。

## Visual Studio Developer PowerShell / Developer Command Prompt

リポジトリのルートから実行してください。

```powershell
msbuild Root/Root.sln /m /p:Configuration=Debug /p:Platform=x64 /p:RavenUITest=true
& "./Root/Root/x64/Debug/RavenUITests.exe"
if ($LASTEXITCODE -ne 0) { throw "UI回帰テストが失敗しました (exit=$LASTEXITCODE)" }
```

実行ファイルの配置先はローカルのMSBuildプロパティや既存の `OutDir` 設定によって変わる場合があります。
その場合はビルドログの出力先にある `RavenUITests.exe` を実行してください。
成功時は終了コード0、期待値と実測値が異なる場合は標準エラーへ差分を出力して終了コード1を返します。

Releaseでも検証できます。

```powershell
msbuild Root/Root.sln /m /p:Configuration=Release /p:Platform=x64 /p:RavenUITest=true
```

通常アプリケーションへ戻すときは `/p:RavenUITest=true` を付けずにビルドします。
テストモードの中間生成物は `$(Platform)\$(Configuration)\UITests\` に分離されます。

**注意:** このモードは既存エンジン全体をビルドするため、通常ビルドと同じVisual Studio C++ツールセット、submodule、Vulkan SDKなどの依存関係が必要です。CIや実機での成功は未確認です。
