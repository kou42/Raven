[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ManifestPath,
    [Parameter(Mandatory)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (Test-Path -LiteralPath $OutputPath) { throw 'Output already exists.' }
$manifestFile = (Resolve-Path -LiteralPath $ManifestPath).Path
$baseDirectory = Split-Path -Parent $manifestFile
$sessions = @(Import-Csv -LiteralPath $manifestFile)
if ($sessions.Count -eq 0) { throw 'Manifest is empty.' }
$culture = [Globalization.CultureInfo]::InvariantCulture
$seen = @{}
$result = foreach ($session in $sessions) {
    foreach ($column in @('backend', 'configuration', 'dpi', 'scenario', 'stage', 'csv')) {
        if ([string]::IsNullOrWhiteSpace($session.$column)) { throw "Missing manifest value: $column" }
    }
    $key = "$($session.backend)/$($session.configuration)/$($session.dpi)/$($session.scenario)/$($session.stage)"
    if ($seen.ContainsKey($key)) { throw "Duplicate session: $key" }
    $seen[$key] = $true
    $csvPath = if ([IO.Path]::IsPathRooted($session.csv)) { $session.csv } else { Join-Path $baseDirectory $session.csv }
    $rows = @(Import-Csv -LiteralPath $csvPath)
    $windowStreams = @($rows.scope | Where-Object { $_ -match '^window_[0-9]+$' } | Sort-Object -Unique).Count
    $omitted = @($rows | Where-Object { $_.scope -eq 'recording' -and $_.metric -eq 'omitted_window_samples' })
    if ($omitted.Count -ne 1) { throw "Missing or duplicate recording metadata in $csvPath" }
    $omittedCount = [UInt64]::Parse($omitted[0].max, $culture)
    $metrics = @{}
    foreach ($row in $rows) {
        if ($row.scope -eq 'all_windows_ui') {
            if ($metrics.ContainsKey($row.metric)) { throw "Duplicate aggregate metric in $csvPath" }
            $samples = [UInt64]::Parse($row.samples, $culture)
            if ($samples -gt 0) {
                foreach ($value in @($row.average, $row.max)) {
                    $number = [double]::Parse($value, $culture)
                    if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -lt 0) { throw "Invalid metric in $csvPath" }
                }
                if ([double]::Parse($row.average, $culture) -gt [double]::Parse($row.max, $culture)) {
                    throw "Average exceeds maximum in $csvPath"
                }
            } elseif ($row.average -ne 'N/A' -or $row.max -ne 'N/A') { throw "Unavailable metric must be N/A in $csvPath" }
            $metrics[$row.metric] = $row
        }
    }
    if ($metrics.ContainsKey('build_ms') -and [UInt64]::Parse($metrics['build_ms'].samples, $culture) -eq 0) {
        throw "No measured frames in $csvPath"
    }
    # 旧MainのみのCSVを全Windowの測定値と取り違えないよう拒否します。
    foreach ($name in @('build_ms','draw_commands','vertices','indices','texture_switches','drawlist_allocations','drawlist_allocated_bytes','drawlist_retained_bytes','elements','lifetime_peak_elements','cpp_allocations','cpp_allocated_bytes')) {
        if ($metrics.ContainsKey($name) -eq $false) { throw "Missing all_windows_ui/$name in $csvPath" }
        $metric = $metrics[$name]
        [pscustomobject]@{
            backend=$session.backend; configuration=$session.configuration; dpi=$session.dpi
            scenario=$session.scenario; stage=$session.stage; metric=$name
            samples=$metric.samples; average=$metric.average; max=$metric.max
            window_streams=$windowStreams; omitted_window_samples=$omittedCount
            source=(Resolve-Path -LiteralPath $csvPath).Path
        }
    }
}
$result | Export-Csv -LiteralPath $OutputPath -NoTypeInformation -Encoding utf8
