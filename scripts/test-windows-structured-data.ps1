[CmdletBinding()]
param([switch]$Benchmark, [switch]$MeasureMemory, [switch]$AllocationTests)
$ErrorActionPreference = 'Stop'
if ($MeasureMemory -and -not $Benchmark) { throw 'MeasureMemory requires Benchmark.' }
if ($AllocationTests -and $Benchmark) { throw 'AllocationTests and Benchmark cannot be combined.' }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'MSVC installation not found.' }
$vcvars = Join-Path ($installation | Select-Object -First 1) 'VC\Auxiliary\Build\vcvars64.bat'
$output = Join-Path $repo 'artifacts\structured-data'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$main = @(
    '#include "Raven/Data/Tests/DataDocumentTests.h"'
    '#include "Raven/Data/Tests/JsonParserTests.h"'
    '#include "Raven/Data/Tests/DataElementTests.h"'
    '#include "Raven/Data/Tests/DataIOTests.h"'
    '#include "Raven/Data/Tests/SerializationTests.h"'
    '#include "Raven/Data/Tests/MessagePackTests.h"'
    '#include "Raven/Data/Tests/StructuredDataTestAsset.h"'
    '#include "Raven/Data/Tests/ParallelDeserializeTests.h"'
    '#include "Raven/Data/Tests/MutableDataDocumentTests.h"'
    '#include "Raven/Data/Tests/CompiledTestAsset.h"'
    '#include "Raven/Network/Tests/ControlMessageTests.h"'
    '#include "Raven/Network/Tests/WindowsTcpTests.h"'
    'int main() { int result = 0;'
)
foreach ($entry in @('RunDataDocumentTests', 'RunJsonParserTests', 'RunDataElementTests', 'RunDataIOTests', 'RunSerializationTests', 'RunMessagePackTests', 'RunTestAssetTests', 'RunParallelDeserializeTests', 'RunMutableDataDocumentTests', 'RunCompiledTestAssetTests')) {
    $main += "result |= Raven::Data::Tests::$entry();"
}
$main += 'result |= Raven::Network::Tests::RunControlMessageTests();'
$main += 'result |= Raven::Network::Tests::RunWindowsTcpTests();'
$main += 'return result; }'
Set-Content -LiteralPath (Join-Path $output 'main.cpp') -Value $main -Encoding utf8
$sources = @('Root/Root/Raven/Core/JsonLexicalReader.cpp', 'Root/Root/Raven/Core/Jobs/JobSystem.cpp')
$sources += Get-ChildItem (Join-Path $repo 'Root/Root/Raven/Data') -Recurse -Filter '*.cpp' |
    ForEach-Object { '"' + $_.FullName + '"' }
$sources += Get-ChildItem (Join-Path $repo 'Root/Root/Raven/Network') -Recurse -Filter '*.cpp' |
    ForEach-Object { '"' + $_.FullName + '"' }
$flags = '/nologo /std:c++17 /utf-8 /EHsc /W4 /IRoot/Root'
$mainSource = 'artifacts/structured-data/main.cpp'
if ($Benchmark) {
    $flags += ' /O2 /DNDEBUG /DRAVEN_DATA_BENCHMARK_MAIN'
    $mainSource = ''
}
if ($MeasureMemory -or $AllocationTests) { $flags += ' /DRAVEN_DATA_BENCHMARK_MEMORY' }
# Core/Dataの同名Parser・Writerのobj衝突を避けます。専用main以外のmain macroは指定しません。
$batch = @(
    '@echo off'
    "call `"$vcvars`" >nul"
    'if errorlevel 1 exit /b %errorlevel%'
    "cl $flags /c Root/Root/Raven/Core/JsonParser.cpp /Foartifacts/structured-data/CoreJsonParser.obj > artifacts/structured-data/msvc-build.log 2>&1"
    'if errorlevel 1 exit /b %errorlevel%'
    "cl $flags /c Root/Root/Raven/Core/JsonWriter.cpp /Foartifacts/structured-data/CoreJsonWriter.obj >> artifacts/structured-data/msvc-build.log 2>&1"
    'if errorlevel 1 exit /b %errorlevel%'
    "cl $flags /Foartifacts/structured-data/ /Feartifacts/structured-data/all-tests.exe $($sources -join ' ') $mainSource artifacts/structured-data/CoreJsonParser.obj artifacts/structured-data/CoreJsonWriter.obj ws2_32.lib >> artifacts/structured-data/msvc-build.log 2>&1"
    'if errorlevel 1 exit /b %errorlevel%'
    'artifacts\structured-data\all-tests.exe > artifacts/structured-data/tests.log 2>&1'
    'exit /b %errorlevel%'
)
$batchPath = Join-Path $output 'msvc-tests.cmd'
Set-Content -LiteralPath $batchPath -Value $batch -Encoding oem
$testLog = Join-Path $output 'tests.log'
Set-Content -LiteralPath $testLog -Value ''
Push-Location $repo
try {
    & cmd /c $batchPath
    $result = $LASTEXITCODE
    $log = Join-Path $output 'msvc-build.log'
    if ($result -ne 0) {
        Get-Content $log | Select-String 'error|warning' | Select-Object -First 30
    }
    $warnings = @(Get-Content $log | Select-String 'warning C\d+|warning LNK\d+').Count
    Write-Output "Structured Data MSVC: exit=$result, warnings=$warnings"
    if (Test-Path $testLog) {
        if ($Benchmark -and $result -eq 0) { Get-Content $testLog -TotalCount 2 }
        else { Get-Content $testLog }
    }
    if ($Benchmark -and $result -eq 0) {
        $name = if ($MeasureMemory) { 'benchmark-memory.csv' } else { 'benchmark-time.csv' }
        $lines = Get-Content $testLog
        $start = 0
        while ($start -lt $lines.Count -and -not $lines[$start].StartsWith('case,stage,')) { ++$start }
        $lines | Select-Object -Skip $start | Set-Content -LiteralPath (Join-Path $output $name) -Encoding utf8
        Write-Output "Benchmark CSV: $(Join-Path $output $name)"
    }
    if ($result -ne 0) { throw "Structured Data verification failed. Logs: $output" }
}
finally { Pop-Location }
