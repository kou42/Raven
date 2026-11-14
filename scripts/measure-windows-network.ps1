[CmdletBinding()]
param([switch]$MeasureMemory)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$mode = if ($MeasureMemory) { 'memory' } else { 'time' }
$output = Join-Path $repo "artifacts/network-benchmarks/$mode"
New-Item -ItemType Directory -Force -Path $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'MSVC installation not found.' }
$vcvars = Join-Path ($installation | Select-Object -First 1) 'VC/Auxiliary/Build/vcvars64.bat'
$sources = @(
    'Core/JsonLexicalReader.cpp',
    'Data/DataDocument.cpp', 'Data/DataElement.cpp', 'Data/DataReader.cpp',
    'Data/DataUtf8.cpp', 'Data/DataWriter.cpp', 'Data/Serialization/Serialization.cpp',
    'Data/Formats/MessagePack/MessagePackParser.cpp', 'Data/Formats/MessagePack/MessagePackWriter.cpp',
    'Data/Tests/DataBenchmarkMemory.cpp', 'Network/ControlMessageCodec.cpp',
    'Network/ControlMessageReceiver.cpp', 'Network/Windows/WindowsTcpSocket.cpp',
    'Network/Tests/ControlMessageBenchmarks.cpp'
) | ForEach-Object { '"' + (Join-Path $repo "Root/Root/Raven/$_") + '"' }
$flags = '/nologo /std:c++17 /utf-8 /EHsc /W4 /O2 /DNDEBUG /DRAVEN_NETWORK_BENCHMARK_MAIN'
if ($MeasureMemory) { $flags += ' /DRAVEN_DATA_BENCHMARK_MEMORY' }
$flags += ' /I"' + (Join-Path $repo 'Root/Root') + '"'
$batchPath = Join-Path $output 'build.cmd'
$buildLog = Join-Path $output 'build.log'
$exe = Join-Path $output 'network-benchmarks.exe'
$batch = @(
    '@echo off'
    "call `"$vcvars`" >nul"
    'if errorlevel 1 exit /b %errorlevel%'
    "cl $flags /Fo`"$output/`" /Fe`"$exe`" $($sources -join ' ') ws2_32.lib > `"$buildLog`" 2>&1"
    'exit /b %errorlevel%'
)
Set-Content -LiteralPath $batchPath -Value $batch -Encoding oem
Push-Location $repo
try {
    & cmd /c $batchPath
    $buildExit = $LASTEXITCODE
    $warnings = @(Get-Content $buildLog | Select-String 'warning C\d+|warning LNK\d+').Count
    if ($buildExit -ne 0) {
        Get-Content $buildLog | Select-String 'error|warning' | Select-Object -First 15
        throw "Network benchmark build failed: exit=$buildExit. Log: $buildLog"
    }
    $pending = Join-Path $output 'results.pending.csv'
    $log = Join-Path $output 'run.log'
    & $exe 1> $pending 2> $log
    $runExit = $LASTEXITCODE
    if ($runExit -ne 0) {
        Get-Content $log -Tail 6
        throw "Network benchmark failed: exit=$runExit. Partial CSV is not a successful result: $pending"
    }
    $rows = @(Import-Csv -LiteralPath $pending)
    if ($rows.Count -ne 30) { throw "Expected 30 benchmark rows, got $($rows.Count)." }
    $keys = @{}
    $samples = if ($MeasureMemory) { 1 } else { 101 }
    foreach ($row in $rows) {
        $key = "$($row.workload)/$($row.stage)/$($row.batch)"
        if ($keys.ContainsKey($key)) { throw "Duplicate row: $key" }
        $keys[$key] = $true
        if ($row.workload -notin @('ping','mixed') -or $row.batch -notin @('1','16','64') -or
            $row.stage -notin @('encode','decode','receive_queue','queue_dispatch','tcp_round_trip') -or
            [int]$row.samples -ne $samples -or [int]$row.checked_messages -ne $samples * [int]$row.batch) {
            throw "Invalid benchmark contract: $key"
        }
        if (-not $MeasureMemory -and @($row.samples_us.Split(';')).Count -ne $samples) {
            throw "Missing time samples: $key"
        }
    }
    $csv = Join-Path $output 'results.csv'
    Move-Item -LiteralPath $pending -Destination $csv -Force
    # これはRateを変えて実測した値ではなく、Frame長からの単純な通信量見積もり。
    $traffic = foreach ($row in $rows | Where-Object { $_.stage -eq 'encode' -and $_.batch -eq '64' }) {
        foreach ($rate in @(30,60,120,240)) {
            [pscustomobject]@{
                workload = $row.workload
                messages_per_second = $rate
                payload_bytes_per_second = [int]$row.payload_bytes / [int]$row.batch * $rate
                stream_bytes_per_second = [int]$row.frame_bytes / [int]$row.batch * $rate
                echo_bytes_per_second = 2 * [int]$row.frame_bytes / [int]$row.batch * $rate
                basis = 'estimate_from_frame_length_excludes_tcp_ip_headers_acks_retransmits'
            }
        }
    }
    $traffic | Export-Csv -LiteralPath (Join-Path $output 'traffic-estimates.csv') -NoTypeInformation -Encoding utf8
    $metadata = [ordered]@{
        measured_at = [DateTimeOffset]::Now.ToString('o')
        mode = $mode
        cpu_identifier = $env:PROCESSOR_IDENTIFIER
        logical_processors = $env:NUMBER_OF_PROCESSORS
        os_version = [Environment]::OSVersion.VersionString
        compiler = (Get-Content $log | Select-Object -First 1)
        flags = $flags
        base_commit = (& git rev-parse HEAD)
        benchmark_sha256 = (Get-FileHash -LiteralPath (Join-Path $repo 'Root/Root/Raven/Network/Tests/ControlMessageBenchmarks.cpp') -Algorithm SHA256).Hash
        script_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
        build_warnings = $warnings
        validation = '30 rows; every measured batch checks message count, FIFO and values'
    }
    $metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'metadata.json') -Encoding utf8
    Write-Output "Network benchmark $mode`: exit=0, warnings=$warnings, rows=$($rows.Count), samples=$samples"
    Write-Output "CSV: $csv"
}
finally { Pop-Location }
