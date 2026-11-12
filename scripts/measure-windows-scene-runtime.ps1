[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Label = 'current',
    [switch]$AllocationProfiling,
    [switch]$PinToFirstAllowedProcessor,
    [switch]$SkipBuild,
    [switch]$SkipBootstrap
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-windows.ps1') -Configuration $Configuration -AllocationProfiling:$AllocationProfiling -SkipBootstrap:$SkipBootstrap
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
$previousPath = $env:Path
if ($Configuration -eq 'Debug') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsRoot = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    $debugCrt = Get-ChildItem (Join-Path $vsRoot 'VC\Redist\MSVC') -Filter vcruntime140d.dll -File -Recurse |
        Where-Object FullName -Match '\\debug_nonredist\\x64\\' | Select-Object -First 1
    $debugUcrt = Get-ChildItem (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin') -Filter ucrtbased.dll -File -Recurse |
        Where-Object FullName -Match '\\x64\\ucrt\\' | Select-Object -First 1
    if (-not $debugCrt -or -not $debugUcrt) { throw 'Debug CRT was not found.' }
    $env:Path = "$($debugCrt.DirectoryName);$($debugUcrt.DirectoryName);$previousPath"
}
$outputDirectory = Join-Path $repoRoot "Root\x64\$Configuration"
if ($AllocationProfiling) { $outputDirectory = Join-Path $outputDirectory 'AllocationProfile' }
$executable = Join-Path $outputDirectory 'Root.exe'
$logDirectory = Join-Path $repoRoot 'artifacts\logs'
New-Item -ItemType Directory -Force $logDirectory | Out-Null
$suffix = if ($AllocationProfiling) { '-allocation' } else { '' }
$logPath = Join-Path $logDirectory "sg5-runtime-$Label-$Configuration$suffix.log"
Push-Location $repoRoot
$measurementProcess = Get-Process -Id $PID
$previousAffinity = $measurementProcess.ProcessorAffinity
try {
    if ($PinToFirstAllowedProcessor) {
        # 子プロセスへ継承させる測定条件です。終了時に現在のPowerShellだけを元へ戻します。
        $allowed = $previousAffinity.ToInt64()
        $firstAllowed = $allowed -band (-$allowed)
        $measurementProcess.ProcessorAffinity = [IntPtr]$firstAllowed
    }
    & $executable --measure-scene-runtime *> $logPath
    $testExit = $LASTEXITCODE
    $marker = Select-String -LiteralPath $logPath -SimpleMatch 'Scene Runtime measurements passed.'
    if ($testExit -ne 0 -or -not $marker) {
        Get-Content $logPath -Tail 15 | ForEach-Object { Write-Host $_ }
        throw "Scene Runtime measurement failed (exit=$testExit). See $logPath"
    }
    Add-Content -LiteralPath $logPath -Value "Measurement processor_mask=$($measurementProcess.ProcessorAffinity.ToInt64()) pinned=$PinToFirstAllowedProcessor"
    Write-Host "Scene Runtime measurement passed ($Configuration, $Label, allocation=$AllocationProfiling). Log: $logPath"
} finally {
    if ($PinToFirstAllowedProcessor) { $measurementProcess.ProcessorAffinity = $previousAffinity }
    $env:Path = $previousPath
    Pop-Location
}
