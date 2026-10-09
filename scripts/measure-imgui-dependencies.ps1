param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string]$OutputJson = ''
)

$ErrorActionPreference = 'Stop'

$ripgrepCommand = Get-Command rg -ErrorAction SilentlyContinue
if ($null -eq $ripgrepCommand)
{
    throw 'rg (ripgrep) is required to measure Dear ImGui dependencies.'
}

$productTargets = @(
    'Root/Root/Raven',
    'Root/Root/main.cpp',
    'Root/Root/Root.vcxproj',
    'Root/Root/Root.vcxproj.filters'
)

Push-Location -LiteralPath $RepositoryRoot
try
{
    # vendor、生成物、移行記録を除外し、通常製品コードとproject設定だけを同じ条件で数えます。
    $commonArguments = @(
        '--glob', '!Root/Root/vendor/**',
        '--glob', '!Root/Root/x64/**',
        '--glob', '!Root/x64/**',
        '--glob', '!*.md'
    )

    $dependencyFiles = @(& rg -l @commonArguments '(ImGui|imgui)' @productTargets 2>$null)
    $apiCalls = @(& rg -o @commonArguments '\bImGui::[A-Za-z_][A-Za-z0-9_]*' @productTargets 2>$null)
    $projectReferences = @(& rg -n -i '(ImGui|imgui)' `
        'Root/Root/Root.vcxproj' 'Root/Root/Root.vcxproj.filters' 2>$null)
    $runtimeConditions = @(& rg -n `
        'EnableDearImGui|CreateScope<ImGuiLayer>|m_ImGuiLayer' `
        'Root/Root/main.cpp' 'Root/Root/Raven/Core/Application.h' `
        'Root/Root/Raven/Core/Application.cpp' 2>$null)

    $result = [pscustomobject]@{
        MeasuredAt = (Get-Date).ToString('o')
        RepositoryRoot = (Resolve-Path -LiteralPath '.').Path
        DependencyFileCount = $dependencyFiles.Count
        ImGuiApiCallCount = $apiCalls.Count
        ProjectReferenceLineCount = $projectReferences.Count
        RuntimeConditionLineCount = $runtimeConditions.Count
        DependencyFiles = @($dependencyFiles | Sort-Object)
        RuntimeConditions = @($runtimeConditions)
    }

    if ([string]::IsNullOrWhiteSpace($OutputJson) -eq $false)
    {
        $outputPath = if ([System.IO.Path]::IsPathRooted($OutputJson))
        {
            $OutputJson
        }
        else
        {
            Join-Path $RepositoryRoot $OutputJson
        }
        $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $outputPath -Encoding utf8
    }

    # 画面表示だけでなくCIや差分確認から詳細一覧を再利用できるよう、結果Objectを返します。
    $result
}
finally
{
    Pop-Location
}
