# Explicit Scene Frame結果の回帰テスト

`ExplicitSceneFrameSelfTests.cpp` は、Window・GPU Deviceを作らずに
`Application::HandleExplicitSceneFrameResult` のCallback呼び出し順序を検証します。

Visual Studio Developer PowerShell / Developer Command Promptで、リポジトリのルートから実行してください。

```powershell
msbuild Root/Root.sln /m /p:Configuration=Debug /p:Platform=x64
& "./Root/Root/x64/Debug/Root.exe" --test-explicit-frame
if ($LASTEXITCODE -ne 0) { throw "Explicit Frame SelfTest failed (exit=$LASTEXITCODE)" }
```

実行ファイル名・配置先は既存のOutDir/TargetName設定に合わせて読み替えてください。
Debugの通常起動でもSelfTestを実行しますが、専用引数ならSceneやWindowを起動せず終了します。
`assert`を使用するため、テスト実行はDebug構成で行ってください。
既存エンジン全体のリンク依存（Visual Studio C++ツールセット、Vulkan SDKなど）は必要です。
このテストはGPU実機でのAcquire/Resize/Presentの成否を検証するものではありません。
