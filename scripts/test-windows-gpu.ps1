[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("x64")]
    [string]$Platform = "x64",

    [switch]$RequireFontAtlas,

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
}

if (-not [Environment]::UserInteractive) {
    throw "GPU integration tests require an interactive logged-in Windows desktop session."
}

$adapters = @(Get-CimInstance -ClassName Win32_VideoController -ErrorAction Stop)
if ($adapters.Count -eq 0) {
    throw "No Windows video adapter was detected."
}
Write-Host "Detected video adapter(s):"
$adapters | ForEach-Object {
    Write-Host ("  {0} (driver {1})" -f $_.Name, $_.DriverVersion)
}

if (-not $SkipBootstrap) {
    & (Join-Path $PSScriptRoot "bootstrap-windows-build.ps1")
}

& (Join-Path $PSScriptRoot "build-windows.ps1") `
    -Configuration $Configuration `
    -Platform $Platform `
    -RavenUIGPUTest `
    -SkipBootstrap
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$expectedTestExecutable = Join-Path $RepoRoot "Root\$Platform\$Configuration\RavenUIGPUTests.exe"
if (Test-Path -LiteralPath $expectedTestExecutable -PathType Leaf) {
    $testExecutable = $expectedTestExecutable
} else {
    $testExecutable = Get-ChildItem -LiteralPath (Join-Path $RepoRoot "Root") `
        -Filter RavenUIGPUTests.exe -File -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $testExecutable -or -not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
    throw "RavenUIGPUTests.exe was not found after a successful GPU test build."
}

if ($Configuration -eq "Debug") {
    Enable-DebugRuntime
}

if (-not $env:RAVEN_UI_TEST_FONT) {
    $fontCandidates = @(
        "C:\Windows\Fonts\arial.ttf",
        "C:\Windows\Fonts\segoeui.ttf",
        "C:\Windows\Fonts\meiryo.ttc"
    )
    $env:RAVEN_UI_TEST_FONT = $fontCandidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
}
if ($RequireFontAtlas -and -not $env:RAVEN_UI_TEST_FONT) {
    throw "A TTF/TTC font is required. Set RAVEN_UI_TEST_FONT to an existing font file."
}

$logDirectory = Join-Path $RepoRoot "artifacts\logs"
$testLog = Join-Path $logDirectory "windows-gpu-tests-$($Configuration.ToLowerInvariant())-$Platform.log"
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null

Write-Host "Running GPU integration tests: $testExecutable"
Push-Location $RepoRoot
try {
    $output = @(& $testExecutable 2>&1)
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}

$output | Set-Content -LiteralPath $testLog -Encoding utf8
$output | ForEach-Object { Write-Host $_ }
$outputText = $output -join [Environment]::NewLine

if ($exitCode -ne 0) {
    $unsignedExitCode = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$exitCode), 0)
    Write-Host ("GPU integration tests failed (exit={0}, hex=0x{1:X8})." -f $exitCode, $unsignedExitCode)
    Write-Host "Test log: $testLog"
    exit $exitCode
}
if ($outputText -notmatch '\[PASS\] GPU Texture upload diagnostics') {
    throw "GPU integration test returned success without the expected PASS marker. See $testLog"
}
if ($RequireFontAtlas -and $outputText -match '\[SKIP\] GPU Font Atlas') {
    throw "GPU Font Atlas coverage was required but skipped. See $testLog"
}

Write-Host "GPU integration tests passed."
Write-Host "Test log: $testLog"


