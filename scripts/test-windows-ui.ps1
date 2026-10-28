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

function Add-RuntimePath {
    param([Parameter(Mandatory)][string]$Path)

    if ($env:Path -notlike "*$Path*") {
        $env:Path = "$Path;$env:Path"
    }
}

function Enable-DebugRuntime {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $visualStudioRoot = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $visualStudioRoot) {
        throw "Visual Studio C++ installation was not found while locating the Debug CRT."
    }

    $debugCrt = Get-ChildItem -LiteralPath (Join-Path ($visualStudioRoot | Select-Object -First 1) "VC\Redist\MSVC") `
        -Filter vcruntime140d.dll -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\debug_nonredist\\x64\\' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $debugCrt) {
        throw "The x64 Visual C++ Debug CRT was not found below the Visual Studio installation."
    }
    Add-RuntimePath -Path $debugCrt.DirectoryName

    $windowsKitsBin = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    $debugUcrt = Get-ChildItem -LiteralPath $windowsKitsBin `
        -Filter ucrtbased.dll -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\ucrt\\ucrtbased\.dll$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $debugUcrt) {
        throw "The x64 Universal C Runtime debug DLL was not found below the Windows SDK."
    }
    Add-RuntimePath -Path $debugUcrt.DirectoryName

    Write-Host "Enabled Visual C++ and Universal CRT debug runtime paths."
}

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

if ($Configuration -eq "Debug") {
    Enable-DebugRuntime
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
    $unsignedExitCode = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$exitCode), 0)
    Write-Host ("CPU UI regression tests failed (exit={0}, hex=0x{1:X8})." -f $exitCode, $unsignedExitCode)
    Write-Host "Test log: $testLog"
    exit $exitCode
}

Write-Host "CPU UI regression tests passed."
Write-Host "Test log: $testLog"


