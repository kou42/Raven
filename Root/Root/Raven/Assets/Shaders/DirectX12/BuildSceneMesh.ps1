# Build DXIL for the DX12 Scene Mesh demo.
# Keep this script ASCII-only: Windows PowerShell 5.1 may decode UTF-8
# without a BOM as the legacy ANSI code page and corrupt string literals.
param(
    [string]$Dxc = "dxc.exe"
)

$ErrorActionPreference = "Stop"
$shader = Join-Path $PSScriptRoot "SceneMesh.hlsl"
$vertex = Join-Path $PSScriptRoot "SceneMesh.vs.dxil"
$pixel = Join-Path $PSScriptRoot "SceneMesh.ps.dxil"

# Prefer PATH, then the Windows SDK directory when it is configured.
$compiler = Get-Command $Dxc -ErrorAction SilentlyContinue
if ($null -eq $compiler -and $env:WindowsSdkVerBinPath) {
    $sdkDxc = Join-Path $env:WindowsSdkVerBinPath "x64/dxc.exe"
    if (Test-Path -LiteralPath $sdkDxc) {
        $Dxc = $sdkDxc
        $compiler = Get-Command $Dxc -ErrorAction SilentlyContinue
    }
}
if ($null -eq $compiler) {
    throw "dxc.exe was not found. Install DXC and add it to PATH."
}

if ((Test-Path -LiteralPath $shader) -eq $false) {
    throw "Shader was not found: $shader"
}

# Check the exit code of each native compiler invocation explicitly.
& $Dxc -T vs_6_0 -E VSMain -Fo $vertex $shader
if ($LASTEXITCODE -ne 0) {
    throw "Vertex Shader DXIL compilation failed (exit: $LASTEXITCODE)"
}
& $Dxc -T ps_6_0 -E PSMain -Fo $pixel $shader
if ($LASTEXITCODE -ne 0) {
    throw "Pixel Shader DXIL compilation failed (exit: $LASTEXITCODE)"
}

Write-Host "DX12 Scene Shader compilation completed:"
Write-Host "  VS: $vertex"
Write-Host "  PS: $pixel"
