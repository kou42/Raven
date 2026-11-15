[CmdletBinding()]
param([string]$Executable)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Executable)) { $Executable = Join-Path $repo 'artifacts\diagnostics\Debug\DiagnosticsTests.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$directory = Join-Path $repo ('artifacts\diagnostics\remote-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$children = @()
function Port {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start(); $number = $listener.LocalEndpoint.Port; $listener.Stop(); return $number
}
function Start-Receiver([string]$name, [int]$port, [int]$duration = 2000, [int]$max = 1048576) {
    $output = Join-Path $directory "$name.rvnd"
    $log = Join-Path $directory "$name.log"
    $process = Start-Process -FilePath $Executable -ArgumentList @("--diagnostics-receive=$port", "--diagnostics-output=`"$output`"", "--diagnostics-duration-ms=$duration", "--diagnostics-max-bytes=$max", '--diagnostics-language=ja') -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $directory "$name.err")
    $script:children += $process
    return @{Process=$process; Output=$output; Log=$log}
}
function Ready($receiver) {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ((Get-Content -LiteralPath $receiver.Log -Raw -ErrorAction SilentlyContinue) -match 'REMOTE_READY') { return }
        if ($receiver.Process.HasExited) { throw 'Receiver exited before ready' }
        Start-Sleep -Milliseconds 20
    }
    throw 'Receiver ready timeout'
}
function End($receiver, [int]$code) {
    if (-not $receiver.Process.WaitForExit(6000)) { throw 'Receiver did not stop within deadline' }
    if ($receiver.Process.ExitCode -ne $code) { throw "Unexpected receiver exit: $($receiver.Process.ExitCode); $($receiver.Log)" }
    return Get-Content -LiteralPath $receiver.Log -Raw -Encoding UTF8
}
function Send-Raw([int]$port, [byte[]]$bytes) {
    $client = [Net.Sockets.TcpClient]::new()
    try { $client.Connect('127.0.0.1', $port); $client.GetStream().Write($bytes, 0, $bytes.Length) }
    finally { $client.Dispose() }
}
function Packet([byte]$kind, [uint64]$session, [uint64]$connection) {
    $bytes = [byte[]]::new(68)
    [BitConverter]::GetBytes([uint32]64).CopyTo($bytes, 0); $bytes[4] = $kind; $bytes[5] = 1
    [BitConverter]::GetBytes($session).CopyTo($bytes, 12); [BitConverter]::GetBytes($connection).CopyTo($bytes, 28)
    return ,$bytes
}
try {
    $port = Port; $receiver = Start-Receiver 'normal' $port; Ready $receiver
    & $Executable "--diagnostics-send-test=$port" *> (Join-Path $directory 'sender.log')
    if ($LASTEXITCODE -ne 0) { throw 'Sample sender failed' }
    $summary = End $receiver 0
    if ($summary -notmatch 'received=5 saved=5' -or $summary -notmatch 'gaps=0 network_drops=0' -or $summary -notmatch '物理の重力') { throw 'Remote events/translation mismatch' }
    $metadata = Get-Content -LiteralPath ($receiver.Output + '.remote.json') -Raw | ConvertFrom-Json
    if ($metadata.received -ne 5 -or $metadata.closed -ne $true -or $metadata.ordinalGaps -ne 0) { throw 'Capture metadata missing' }
    $read = & $Executable "--diagnostics-inspect=$($receiver.Output)"
    if ($LASTEXITCODE -ne 0 -or $read -notmatch 'events=5 counters=1 begins=1 ends=1 messages=1') { throw 'Saved Binary replay mismatch' }
    $length = (Get-Item -LiteralPath $receiver.Output).Length
    # 既存Captureは変更せず終了します。
    $again = Start-Receiver 'normal' (Port)
    $null = End $again 1
    if ((Get-Item -LiteralPath $receiver.Output).Length -ne $length) { throw 'Existing capture modified' }
    $port = Port; $limited = Start-Receiver 'limited' $port 2000 16; Ready $limited
    & $Executable "--diagnostics-send-test=$port" *> (Join-Path $directory 'limited-sender.log')
    $summary = End $limited 1
    if ($summary -notmatch 'save_failed=1' -or (Get-Item -LiteralPath $limited.Output).Length -gt 16) { throw 'Capture limit failed' }
    $idle = Start-Receiver 'idle' (Port) 200; Ready $idle; $null = End $idle 1
    if (Test-Path -LiteralPath $idle.Output) { throw 'Idle receiver created capture without Session' }
    $port = Port; $sessions = Start-Receiver 'sessions' $port 4000; Ready $sessions
    Send-Raw $port ([byte[]](255,255,255,255))
    Send-Raw $port ([byte[]](64,0))
    Send-Raw $port (Packet 1 123 1)
    Send-Raw $port (Packet 1 999 2)
    $hello = Packet 1 123 2; $close = Packet 4 123 2
    Send-Raw $port ($hello + $close)
    $summary = End $sessions 0
    if ($summary -notmatch 'received=0 saved=0' -or $summary -notmatch 'connections=2' -or $summary -notmatch 'rejected=2') { throw 'Session isolation/reconnect/framing failed' }
    Write-Output 'Diagnostics remote processes: scenarios=5 failures=0 (transfer/replay, overwrite refusal, cap, idle timeout, malformed/reconnect/session isolation)'
} finally {
    foreach ($process in $children) { if ($process.HasExited -eq $false) { Stop-Process -Id $process.Id -Force } }
}
