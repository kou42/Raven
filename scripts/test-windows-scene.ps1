[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$SkipBootstrap
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$logDirectory = Join-Path $RepoRoot "artifacts\logs"
$testDirectory = Join-Path $RepoRoot "artifacts\scene-tests"
New-Item -ItemType Directory -Force -Path $logDirectory, $testDirectory | Out-Null

if (-not $SkipBootstrap) {
    & (Join-Path $PSScriptRoot "bootstrap-windows-build.ps1")
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$visualStudioRoot = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $visualStudioRoot) {
    throw "Visual Studio C++ installation was not found."
}
$visualStudioRoot = $visualStudioRoot | Select-Object -First 1
$vcvars = Join-Path $visualStudioRoot "VC\Auxiliary\Build\vcvars64.bat"
$storageSource = Join-Path $RepoRoot "Root\Tests\SceneStorageTests.cpp"
$includeDirectory = Join-Path $RepoRoot "Root\Root"
$storageExecutable = Join-Path $testDirectory "SceneStorageTests.exe"
$storageObject = Join-Path $testDirectory "SceneStorageTests.obj"
$compileLog = Join-Path $logDirectory "windows-scene-storage-build.log"

# /MTで独立テストのCRT依存を閉じ、/UNDEBUGでassertによる検証を必ず有効にします。
# /WXはこの独立したテスト翻訳単位にだけ適用し、Engine全体の既存Warningとは分離します。
$compileCommand = "call `"$vcvars`" >nul && cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /MT /UNDEBUG /I `"$includeDirectory`" `"$storageSource`" /Fe:`"$storageExecutable`" /Fo:`"$storageObject`""
$compileOutput = @(& $env:ComSpec /d /s /c $compileCommand 2>&1)
$compileExitCode = $LASTEXITCODE
Set-Content -LiteralPath $compileLog -Value ($compileOutput -join [Environment]::NewLine) -Encoding utf8
Add-Content -LiteralPath $compileLog -Value "Storage compile exit=$compileExitCode" -Encoding utf8
if ($compileExitCode -ne 0) {
    $compileOutput | ForEach-Object { Write-Host $_ }
    throw "Scene storage compilation failed (exit=$compileExitCode). See $compileLog"
}

$storageOutput = @(& $storageExecutable 2>&1)
$storageExitCode = $LASTEXITCODE
$storageLog = Join-Path $logDirectory "windows-scene-storage-results.log"
Set-Content -LiteralPath $storageLog -Value ($storageOutput -join [Environment]::NewLine) -Encoding utf8
Add-Content -LiteralPath $storageLog -Value "Scene storage tests exit=$storageExitCode" -Encoding utf8
if ($storageExitCode -ne 0) {
    $storageOutput | ForEach-Object { Write-Host $_ }
    throw "Scene storage tests failed (exit=$storageExitCode). See $storageLog"
}
Write-Host "Scene storage tests passed."

$mathSource = Join-Path $RepoRoot "Root\Tests\TransformMathTests.cpp"
$mathImplementation = Join-Path $RepoRoot "Root\Root\Raven\Math\TransformMath.cpp"
$quaternionImplementation = Join-Path $RepoRoot "Root\Root\Raven\Math\MathQuatanion.cpp"
$mathExecutable = Join-Path $testDirectory "TransformMathTests.exe"
$mathCommand = "call `"$vcvars`" >nul && cd /d `"$testDirectory`" && cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /MT /UNDEBUG /I `"$includeDirectory`" `"$mathSource`" `"$mathImplementation`" `"$quaternionImplementation`" /Fe:`"$mathExecutable`""
$mathOutput = @(& $env:ComSpec /d /s /c $mathCommand 2>&1)
$mathExit = $LASTEXITCODE
$mathLog = Join-Path $logDirectory "windows-scene-transform-math-build.log"
Set-Content -LiteralPath $mathLog -Value ($mathOutput -join [Environment]::NewLine) -Encoding utf8
if ($mathExit -ne 0) {
    $mathOutput | ForEach-Object { Write-Host $_ }
    throw "Transform math compilation failed (exit=$mathExit). See $mathLog"
}
$mathOutput = @(& $mathExecutable 2>&1)
$mathExit = $LASTEXITCODE
$mathLog = Join-Path $logDirectory "windows-scene-transform-math-results.log"
Set-Content -LiteralPath $mathLog -Value ($mathOutput -join [Environment]::NewLine) -Encoding utf8
Add-Content -LiteralPath $mathLog -Value "Transform math tests exit=$mathExit" -Encoding utf8
if ($mathExit -ne 0) {
    $mathOutput | ForEach-Object { Write-Host $_ }
    throw "Transform math tests failed (exit=$mathExit). See $mathLog"
}
Write-Host "Transform math tests passed."

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build-windows.ps1") -Configuration Debug -Platform x64 -SkipBootstrap
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

# Root.exeはDebug CRTを使用します。既存UIテストと同じSDK配置からDLL検索先を追加します。
$debugCrt = Get-ChildItem -LiteralPath (Join-Path $visualStudioRoot "VC\Redist\MSVC") `
    -Filter vcruntime140d.dll -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\debug_nonredist\\x64\\' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
$windowsKitsBin = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
$debugUcrt = Get-ChildItem -LiteralPath $windowsKitsBin `
    -Filter ucrtbased.dll -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\ucrt\\ucrtbased\.dll$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $debugCrt -or -not $debugUcrt) {
    throw "The x64 MSVC / Universal CRT debug runtime was not found."
}
$previousPath = $env:Path
$testExecutable = Join-Path $RepoRoot "Root\x64\Debug\Root.exe"
if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
    throw "The Debug application executable was not found: $testExecutable"
}

# テスト専用引数はWindowを作らず終了します。成功メッセージも照合します。
$checks = @(
    @{ Argument = "--test-editor-scene-lifetime"; Marker = "Editor Scene lifetime self-tests passed."; Log = "editor-lifetime" },
    @{ Argument = "--test-scene-lifecycle"; Marker = "Scene lifecycle self-tests passed."; Log = "lifecycle" },
    @{ Argument = "--test-scene-transforms"; Marker = "Scene transform self-tests passed."; Log = "transforms" },
    @{ Argument = "--test-scene-runtime"; Marker = "Scene Runtime comparison tests passed."; Log = "runtime" },
    @{ Argument = "--test-scene-view"; Marker = "Scene View self-tests passed."; Log = "view" },
    @{ Argument = "--test-scene-simulation"; Marker = "Scene simulation self-tests passed."; Log = "simulation" },
    @{ Argument = "--test-physics-collision"; Marker = "Physics collision self-tests passed."; Log = "physics-collision" },
    @{ Argument = "--test-scene-graph"; Marker = "Scene graph self-tests passed."; Log = "graph" },
    @{ Argument = "--test-editor-entity-commands"; Marker = "Editor entity command self-tests passed."; Log = "editor" },
    @{ Argument = "--measure-scene-graph"; Marker = "SceneGraph shape=chain"; Log = "measure-debug" }
)
$failedChecks = @()
Push-Location $RepoRoot
try {
    $env:Path = "$($debugCrt.DirectoryName);$($debugUcrt.DirectoryName);$previousPath"
    foreach ($check in $checks) {
        $testOutput = @(& $testExecutable $check.Argument 2>&1)
        $testExitCode = $LASTEXITCODE
        $testLog = Join-Path $logDirectory "windows-scene-$($check.Log)-results.log"
        Set-Content -LiteralPath $testLog -Value ($testOutput -join [Environment]::NewLine) -Encoding utf8
        Add-Content -LiteralPath $testLog -Value "$($check.Argument) exit=$testExitCode" -Encoding utf8
        if ($testExitCode -ne 0 -or ($testOutput -join "`n").Contains($check.Marker) -eq $false) {
            $testOutput | ForEach-Object { Write-Host $_ }
            # 失敗したgroupがあってもGraph / Editor回帰を最後まで確認し、全成功にはしません。
            $failedChecks += "$($check.Argument) failed (exit=$testExitCode). See $testLog"
            continue
        }
        Write-Host "$($check.Argument) passed."
    }
} finally {
    $env:Path = $previousPath
    Pop-Location
}
if ($failedChecks.Count -gt 0) {
    throw ($failedChecks -join [Environment]::NewLine)
}
Write-Host "Scene regression tests passed. Logs: $logDirectory"
