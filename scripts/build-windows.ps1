[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("x64")]
    [string]$Platform = "x64",

    [switch]$SkipBootstrap
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if (-not $SkipBootstrap) {
    & (Join-Path $PSScriptRoot "bootstrap-windows-build.ps1")
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "Visual Studio Installer (vswhere.exe) was not found."
}

$installationPath = & $vswhere -latest -products * `
    -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installationPath) {
    throw "Visual Studio 2022 with MSBuild and the MSVC v143 C++ toolset was not found."
}

$msbuild = Join-Path ($installationPath | Select-Object -First 1) "MSBuild\Current\Bin\MSBuild.exe"
$solution = Join-Path $RepoRoot "Root\Root.sln"
$logDirectory = Join-Path $RepoRoot "artifacts\logs"
$logPath = Join-Path $logDirectory "windows-$($Configuration.ToLowerInvariant())-$Platform.log"
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null

$arguments = @(
    $solution
    "/m"
    "/nologo"
    "/verbosity:minimal"
    "/p:Configuration=$Configuration"
    "/p:Platform=$Platform"
    "/fl"
    "/flp:LogFile=$logPath;Verbosity=normal"
)

Write-Host "Building Root/Root.sln ($Configuration|$Platform)"
& $msbuild @arguments
$exitCode = $LASTEXITCODE

if ($exitCode -ne 0) {
    Write-Host "MSBuild failed (exit=$exitCode). Relevant diagnostics:"
    if (Test-Path -LiteralPath $logPath -PathType Leaf) {
        Get-Content -LiteralPath $logPath |
            Select-String -Pattern '(?i)fatal error|\berror\b|LNK\d{4}|MSB\d{4}' |
            Select-Object -Last 100 |
            ForEach-Object { Write-Host $_.Line }
    }
    Write-Host "Full build log: $logPath"
    exit $exitCode
}

$warningCount = 0
if (Test-Path -LiteralPath $logPath -PathType Leaf) {
    $warningCount = @(Get-Content -LiteralPath $logPath | Select-String -Pattern '(?i): warning [A-Z]+\d+:').Count
}

Write-Host "Build succeeded with $warningCount compiler/linker warning(s)."
Write-Host "Full build log: $logPath"


