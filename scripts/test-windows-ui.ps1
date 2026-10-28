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

& (Join-Path $PSScriptRoot "build-windows.ps1") `
    -Configuration $Configuration `
    -Platform $Platform `
    -RavenUITest `
    -SkipBootstrap
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$expectedTestExecutable = Join-Path $RepoRoot "Root\$Platform\$Configuration\RavenUITests.exe"
if (Test-Path -LiteralPath $expectedTestExecutable -PathType Leaf) {
    $testExecutable = $expectedTestExecutable
} else {
    $testExecutable = Get-ChildItem -LiteralPath (Join-Path $RepoRoot "Root") `
        -Filter RavenUITests.exe -File -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

if (-not $testExecutable -or -not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
    throw "RavenUITests.exe was not found after a successful test build."
}

$windowsFont = "C:\Windows\Fonts\arial.ttf"
if (-not $env:RAVEN_UI_TEST_FONT -and (Test-Path -LiteralPath $windowsFont -PathType Leaf)) {
    $env:RAVEN_UI_TEST_FONT = $windowsFont
}

$logDirectory = Join-Path $RepoRoot "artifacts\logs"
$testLog = Join-Path $logDirectory "windows-ui-tests-$($Configuration.ToLowerInvariant())-$Platform.log"
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null

Write-Host "Running CPU UI regression tests: $testExecutable"
Push-Location $RepoRoot
try {
    $output = @(& $testExecutable 2>&1)
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}

$output | Set-Content -LiteralPath $testLog -Encoding utf8
$output | ForEach-Object { Write-Host $_ }

if ($exitCode -ne 0) {
    Write-Host "CPU UI regression tests failed (exit=$exitCode)."
    Write-Host "Test log: $testLog"
    exit $exitCode
}

Write-Host "CPU UI regression tests passed."
Write-Host "Test log: $testLog"


