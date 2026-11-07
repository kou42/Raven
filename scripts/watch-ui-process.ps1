[CmdletBinding()]
param(
    [Parameter(Mandatory)][int]$TargetProcessId,
    [Parameter(Mandatory)][string]$OutputPath,
    [ValidateRange(1, 86400)][int]$DurationSeconds = 1800,
    [ValidateRange(1, 60)][int]$IntervalSeconds = 10
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (Test-Path -LiteralPath $OutputPath) { throw 'Output already exists.' }
$target = Get-Process -Id $TargetProcessId
$started = $target.StartTime.ToUniversalTime()
$timer = [Diagnostics.Stopwatch]::StartNew()
$culture = [Globalization.CultureInfo]::InvariantCulture
$writer = [IO.StreamWriter]::new($OutputPath, $false, [Text.UTF8Encoding]::new($false))
try {
    $writer.WriteLine('utc,elapsed_seconds,pid,working_set_bytes,private_bytes,handles,threads,cpu_seconds,status')
    while ($true) {
        $target.Refresh()
        $elapsed = $timer.Elapsed.TotalSeconds.ToString('F3', $culture)
        $utc = [DateTime]::UtcNow.ToString('o', $culture)
        if ($target.HasExited) {
            $writer.WriteLine("$utc,$elapsed,$TargetProcessId,,,,,,exited")
            break
        }
        # PID再利用を別セッションの継続として集計しないため、開始時刻も照合します。
        if ($target.StartTime.ToUniversalTime() -ne $started) { throw 'Process identity changed.' }
        $cpu = $target.TotalProcessorTime.TotalSeconds.ToString('F3', $culture)
        $writer.WriteLine("$utc,$elapsed,$TargetProcessId,$($target.WorkingSet64),$($target.PrivateMemorySize64),$($target.HandleCount),$($target.Threads.Count),$cpu,running")
        $writer.Flush()
        if ($timer.Elapsed.TotalSeconds -ge $DurationSeconds) { break }
        Start-Sleep -Seconds ([Math]::Min($IntervalSeconds, [Math]::Max(1, $DurationSeconds - [int]$timer.Elapsed.TotalSeconds)))
    }
} finally { $writer.Dispose(); $target.Dispose() }
