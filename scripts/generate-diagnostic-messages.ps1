[CmdletBinding()]
param(
    [switch]$Check,
    [switch]$UpdateLock,
    [string]$Catalog,
    [string]$OutputDirectory,
    [string]$LockFile
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrEmpty($Catalog)) { $Catalog = Join-Path $PSScriptRoot '..\Root\Root\Raven\Diagnostics\Messages.json' }
if ([string]::IsNullOrEmpty($OutputDirectory)) { $OutputDirectory = Join-Path $PSScriptRoot '..\Root\Root\Raven\Diagnostics\Generated' }
if ([string]::IsNullOrEmpty($LockFile)) { $LockFile = Join-Path $PSScriptRoot '..\Root\Root\Raven\Diagnostics\Messages.lock.json' }
# PowerShell 5.1と7の双方で同一UTF-8/LFを生成し、ビルド環境差を成果物へ持ち込みません。
$utf8 = New-Object System.Text.UTF8Encoding($false)
function Write-Deterministic([string]$path, [string]$text) {
    $text = $text.Replace("`r`n", "`n") + "`n"
    if ($Check) {
        if (-not (Test-Path -LiteralPath $path) -or [IO.File]::ReadAllText($path) -cne $text) { throw "Generated metadata is stale: $path" }
    } elseif (-not (Test-Path -LiteralPath $path) -or [IO.File]::ReadAllText($path) -cne $text) {
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($path))) | Out-Null
        [IO.File]::WriteAllText($path, $text, $utf8)
    }
}
function CppString([string]$text) {
    return '"' + $text.Replace('\','\\').Replace('"','\"').Replace("`n",'\n').Replace("`r",'\r').Replace("`t",'\t') + '"'
}
$catalogData = Get-Content -LiteralPath $Catalog -Raw -Encoding UTF8 | ConvertFrom-Json
if ($catalogData.dictionaryVersion -ne 1) { throw 'Unsupported Dictionary version' }
$messages = @($catalogData.messages | Sort-Object { [uint64]$_.id })
if ($messages.Count -eq 0 -or $messages.Count -gt 1024) { throw 'Invalid message count' }
$ids = @{}; $keys = @{}; $symbols = @{}; $records = @(); $typed = @()
$types = @{Integer='std::int64_t';Real='double';Boolean='bool';Text='std::string'}
foreach ($message in $messages) {
    if ($message.id -le 0 -or $message.id -gt [uint32]::MaxValue -or $ids.ContainsKey([string]$message.id)) { throw "Invalid/duplicate Message ID: $($message.id)" }
    if ($message.key -cnotmatch '^[A-Za-z][A-Za-z0-9]*$' -or $keys.ContainsKey([string]$message.key)) { throw "Invalid/duplicate Message key: $($message.key)" }
    $ids[[string]$message.id] = $true; $keys[[string]$message.key] = $true
    $hash = 'reserved'; $version = 0
    if ($message.reserved -ne $true) {
        $version = [int]$message.schemaVersion
        foreach ($symbol in @($message.key, ('Make' + $message.key))) {
            if ($symbols.ContainsKey($symbol)) { throw 'Generated C++ symbol collision' }
            $symbols[$symbol] = $true
        }
        if ($version -lt 1 -or $version -gt 65535) { throw 'Invalid schema version' }
        if ($message.category -cnotmatch '^(Core|RHI|Renderer|Physics|Animation|Asset|Runtime|SoftBody)$' -or $message.severity -cnotmatch '^(Info|Warning|Error)$') { throw 'Invalid category/severity' }
        $arguments = @($message.arguments)
        if ($arguments.Count -gt 8) { throw 'Too many message arguments' }
        $names = @{}; $signature = "1|$($message.id)|$version|$($message.category)|$($message.severity)"
        foreach ($argument in $arguments) {
            if ($argument.name -cnotmatch '^[a-z][a-zA-Z0-9]*$' -or $names.ContainsKey([string]$argument.name) -or -not $types.ContainsKey([string]$argument.type)) { throw 'Invalid/duplicate argument name or type' }
            $names[[string]$argument.name] = $true; $signature += "|$($argument.name):$($argument.type)"
        }
        foreach ($language in @('en','ja')) {
            $template = [string]$message.$language
            if ([string]::IsNullOrWhiteSpace($template) -or $utf8.GetByteCount($template) -gt 1024 -or $template -match '[\x00-\x08\x0b\x0c\x0e-\x1f]') { throw 'Invalid message template' }
            $seen = @{}
            $placeholders = [regex]::Matches($template, '\{([a-z][a-zA-Z0-9]*)\}')
            if ($placeholders.Count -gt 8) { throw 'Too many template substitutions' }
            foreach ($match in $placeholders) {
                if (-not $names.ContainsKey($match.Groups[1].Value)) { throw 'Unknown template argument' }
                $seen[$match.Groups[1].Value] = $true
            }
            if ([regex]::Replace($template, '\{([a-z][a-zA-Z0-9]*)\}', '') -match '[{}]' -or $seen.Count -ne $names.Count) { throw 'Missing argument or invalid placeholder syntax' }
        }
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = ([BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($signature)))).Replace('-','').Substring(0,16).ToLowerInvariant() } finally { $sha.Dispose() }
        $message | Add-Member -NotePropertyName hash -NotePropertyValue $hash
        $typed += $message
    }
    $records += [ordered]@{id=[uint64]$message.id;key=[string]$message.key;version=$version;hash=$hash}
}
# 過去IDは削除・再利用せず、同一Schema版の意味変更も許可しません。翻訳文の変更はhashへ含めません。
$history = @()
if (Test-Path -LiteralPath $LockFile) { $history = @((Get-Content -LiteralPath $LockFile -Raw -Encoding UTF8 | ConvertFrom-Json).history) }
foreach ($record in $records) {
    $sameId = @($history | Where-Object { $_.id -eq $record.id })
    if (@($sameId | Where-Object { $_.key -cne $record.key }).Count -gt 0 -or @($history | Where-Object { $_.key -ceq $record.key -and $_.id -ne $record.id }).Count -gt 0) { throw 'Message ID reuse is forbidden' }
    $sameVersion = @($sameId | Where-Object { $_.version -eq $record.version })
    if ($sameId.Count -gt 0 -and (($record.version -eq 0) -ne (@($sameId | Where-Object { $_.version -eq 0 }).Count -gt 0))) { throw 'Reserved IDs cannot change kind' }
    if ($sameVersion.Count -gt 0 -and $sameVersion[0].hash -cne $record.hash) { throw 'Schema changed without a new version' }
    if ($sameVersion.Count -eq 0) {
        if (-not $UpdateLock -or $Check) { throw 'Unregistered schema; review and run -UpdateLock' }
        if ($sameId.Count -gt 0 -and $record.version -le ($sameId | Measure-Object version -Maximum).Maximum) { throw 'Schema version must increase' }
        $history += $record
    }
}
if ($UpdateLock) { Write-Deterministic $LockFile (ConvertTo-Json -Depth 8 -InputObject ([ordered]@{history=@($history | Sort-Object id,version)})) }
$header = @('// Generated by scripts/generate-diagnostic-messages.ps1. Do not edit.', '#pragma once', '#include "Raven/Diagnostics/MessageDictionary.h"', 'namespace Raven::Diagnostics::Messages', '{')
$source = @('// Generated by scripts/generate-diagnostic-messages.ps1. Do not edit.', '#include "Raven/Diagnostics/Generated/MessageMetadata.generated.h"', '#include <array>', '#include <utility>', 'namespace Raven::Diagnostics', '{', 'namespace', '{')
foreach ($message in $typed) {
    $args = @($message.arguments); $key = $message.key
    $header += "inline constexpr DiagnosticEventId $key = $($message.id)ull;"
    $parameters = @($args | ForEach-Object { "$($types[$_.type]) argument_$($_.name)" }) -join ', '
    $header += "DiagnosticMessage Make$key($parameters);"
    $source += "constexpr std::array<MessageArgumentDefinition, $($args.Count)> ${key}Arguments{{"
    foreach ($argument in $args) { $source += ('    {' + (CppString $argument.name) + ', DiagnosticArgumentType::' + $argument.type + '},') }
    $source += '}};'
}
$source += "constexpr std::array<MessageDefinition, $($typed.Count)> Definitions{{"
foreach ($message in $typed) { $source += "    {$($message.id)ull, $(CppString $message.key), $($message.schemaVersion), 0x$($message.hash)ull, DiagnosticCategory::$($message.category), DiagnosticSeverity::$($message.severity), $($message.key)Arguments, $(CppString $message.en), $(CppString $message.ja)}," }
$source += @('}};', '}', 'std::span<const MessageDefinition> GetMessageDefinitions() noexcept { return Definitions; }', 'namespace Messages', '{')
foreach ($message in $typed) {
    $args = @($message.arguments)
    $parameters = @($args | ForEach-Object { "$($types[$_.type]) argument_$($_.name)" }) -join ', '
    $values = @($args | ForEach-Object { if ($_.type -eq 'Text') { "DiagnosticArgument{std::move(argument_$($_.name))}" } else { "DiagnosticArgument{argument_$($_.name)}" } }) -join ', '
    $source += @("DiagnosticMessage Make$($message.key)($parameters)", '{', "    return {1, $($message.schemaVersion), 0x$($message.hash)ull, {$values}};", '}')
}
$header += '}'; $source += @('}', '}')
Write-Deterministic (Join-Path $OutputDirectory 'MessageMetadata.generated.h') ($header -join "`n")
Write-Deterministic (Join-Path $OutputDirectory 'MessageMetadata.generated.cpp') ($source -join "`n")
Write-Output "Diagnostics metadata: $($messages.Count) reserved/typed IDs, $($typed.Count) schemas verified."
