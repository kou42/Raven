# Build DXIL for the Explicit Raven UI shader.
# Keep this script ASCII-only for Windows PowerShell 5.1 compatibility.
param(
    [string]$Dxc = "dxc.exe"
)
$ErrorActionPreference = "Stop"
$shader = Join-Path $PSScriptRoot "UI.hlsl"
$vertex = Join-Path $PSScriptRoot "UI.vs.dxil"
$pixel = Join-Path $PSScriptRoot "UI.ps.dxil"
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
& $Dxc -T vs_6_0 -E VSMain -Fo $vertex $shader
if ($LASTEXITCODE -ne 0) { throw "UI Vertex Shader DXIL compilation failed." }
& $Dxc -T ps_6_0 -E PSMain -Fo $pixel $shader
if ($LASTEXITCODE -ne 0) { throw "UI Pixel Shader DXIL compilation failed." }
