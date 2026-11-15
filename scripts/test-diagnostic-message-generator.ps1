[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$directory = Join-Path $repo ('artifacts\diagnostics\generator-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$generator = Join-Path $PSScriptRoot 'generate-diagnostic-messages.ps1'
$original = Join-Path $repo 'Root\Root\Raven\Diagnostics\Messages.json'
$catalog = Join-Path $directory 'Messages.json'
$lock = Join-Path $directory 'Messages.lock.json'
$output = Join-Path $directory 'Generated'
Copy-Item -LiteralPath (Join-Path $repo 'Root\Root\Raven\Diagnostics\Messages.lock.json') -Destination $lock
$utf8 = New-Object System.Text.UTF8Encoding($false)
$script:checks = 0
function Write-Catalog($data) { [IO.File]::WriteAllText($catalog, (ConvertTo-Json -InputObject $data -Depth 12), $utf8) }
function Fresh-Catalog { return (Get-Content -LiteralPath $original -Raw -Encoding UTF8 | ConvertFrom-Json) }
function Reject($data, [string]$reason) {
    Write-Catalog $data
    $rejected = $false
    try { $null = & $generator -Catalog $catalog -LockFile $lock -OutputDirectory $output }
    catch { if ($_.Exception.Message -notlike "*$reason*") { throw }; $rejected = $true }
    if (-not $rejected) { throw "Generator accepted invalid input: $reason" }
    $script:checks++
}
Write-Catalog (Fresh-Catalog)
$null = & $generator -Catalog $catalog -LockFile $lock -OutputDirectory $output
$null = & $generator -Catalog $catalog -LockFile $lock -OutputDirectory $output -Check
$before = [IO.File]::ReadAllBytes((Join-Path $output 'MessageMetadata.generated.cpp'))
$null = & $generator -Catalog $catalog -LockFile $lock -OutputDirectory $output
if ([Convert]::ToBase64String($before) -cne [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $output 'MessageMetadata.generated.cpp')))) { throw 'Generation is not deterministic' }
$script:checks += 2
$data = Fresh-Catalog; $data.messages += $data.messages[0]; Reject $data 'duplicate Message ID'
$data = Fresh-Catalog; $data.messages[1].key = $data.messages[0].key; Reject $data 'duplicate Message key'
$data = Fresh-Catalog; $data.messages[0].arguments[0].type = 'Pointer'; Reject $data 'argument name or type'
$data = Fresh-Catalog; $data.messages[0].en = 'Missing argument'; Reject $data 'Missing argument'
$data = Fresh-Catalog; $data.messages[0].en = '{unknown}'; Reject $data 'Unknown template'
$data = Fresh-Catalog; $data.messages[0].en = '{reason}' * 9; Reject $data 'Too many template'
$data = Fresh-Catalog; $data.messages[0].ja = ''; Reject $data 'Invalid message template'
$data = Fresh-Catalog; $data.messages[0].arguments[0].type = 'Integer'; Reject $data 'Schema changed'
$data = Fresh-Catalog; $data.messages[0].id = 123456; Reject $data 'ID reuse'
$data = Fresh-Catalog; $data.messages[0].schemaVersion = 2; Reject $data 'Unregistered schema'
$data = Fresh-Catalog; $data.messages[0].key = 'MakePhysicsGravityUpdated'; Reject $data 'symbol collision'
$data = Fresh-Catalog; $data.messages[0] | Add-Member -NotePropertyName reserved -NotePropertyValue $true; Reject $data 'Reserved IDs'
# 翻訳文だけならSchemaを変えません。陳腐化した生成物のCheckは必ず失敗します。
$data = Fresh-Catalog; $data.messages[0].en = 'New text: {reason}'; Write-Catalog $data
$failed = $false
try { $null = & $generator -Catalog $catalog -LockFile $lock -OutputDirectory $output -Check }
catch { if ($_.Exception.Message -notlike '*stale*') { throw }; $failed = $true }
if (-not $failed) { throw 'Check accepted stale metadata' }
$null = & $generator -Catalog $catalog -LockFile $lock -OutputDirectory $output
$null = & $generator -Catalog $catalog -LockFile $lock -OutputDirectory $output -Check
$script:checks++
# 明示的なSchema更新は履歴へ追記し、過去のID/版/hashを保持します。
$data = Fresh-Catalog; $data.messages[0].schemaVersion = 2; $data.messages[0].arguments[0].type = 'Integer'; Write-Catalog $data
$null = & $generator -Catalog $catalog -LockFile $lock -OutputDirectory $output -UpdateLock
$history = @((Get-Content -LiteralPath $lock -Raw -Encoding UTF8 | ConvertFrom-Json).history | Where-Object { $_.id -eq 4097 })
if ($history.Count -ne 2 -or @($history | Where-Object { $_.version -eq 1 }).Count -ne 1) { throw 'Schema history lost' }
$script:checks++
Write-Output "Diagnostics generator: checks=$script:checks failures=0"
