# DX12 Scene Mesh用DXILを生成します。
# Windows SDKまたはDXC配布版のdxc.exeをPATHに追加してから実行してください。
param(
    [string]$Dxc = "dxc.exe"
)

$ErrorActionPreference = "Stop"
$shader = Join-Path $PSScriptRoot "SceneMesh.hlsl"
$vertex = Join-Path $PSScriptRoot "SceneMesh.vs.dxil"
$pixel = Join-Path $PSScriptRoot "SceneMesh.ps.dxil"

if ((Test-Path -LiteralPath $shader) -eq $false) {
    throw "Shaderが見つかりません: $shader"
}

# PowerShellはnative processの非ゼロ終了を常に例外化しないため、
# 各Stageの終了コードを明示的に確認します。
& $Dxc -T vs_6_0 -E VSMain -Fo $vertex $shader
if ($LASTEXITCODE -ne 0) {
    throw "Vertex ShaderのDXIL生成に失敗しました (exit: $LASTEXITCODE)"
}
& $Dxc -T ps_6_0 -E PSMain -Fo $pixel $shader
if ($LASTEXITCODE -ne 0) {
    throw "Pixel ShaderのDXIL生成に失敗しました (exit: $LASTEXITCODE)"
}

Write-Host "DX12 Scene Shader生成完了:"
Write-Host "  VS: $vertex"
Write-Host "  PS: $pixel"
