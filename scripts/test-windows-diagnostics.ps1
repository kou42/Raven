[CmdletBinding()]
param([ValidateSet('Debug', 'Release', 'Disabled', 'MeasurementsDisabled')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'MSVC installation not found.' }
$vcvars = Join-Path ($installation | Select-Object -First 1) 'VC\Auxiliary\Build\vcvars64.bat'
$output = Join-Path $repo "artifacts\diagnostics\$Configuration"
New-Item -ItemType Directory -Force -Path $output | Out-Null
$main = @'
#include "Raven/Diagnostics/DiagnosticAPI.h"
#include "Raven/Diagnostics/DiagnosticMeasurement.h"
#include "Raven/Diagnostics/Tests/DiagnosticSelfTests.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
int main()
{
    try
    {
#if RAVEN_ENABLE_DIAGNOSTICS && RAVEN_ENABLE_DIAGNOSTIC_MEASUREMENTS
        Raven::tests::RunDiagnosticSelfTests();
        std::cout << "Diagnostics: suites=3 failures=0\n";
#else
        using namespace Raven::Diagnostics;
        int evaluated = 0;
        SetDiagnosticsEnabled(true);
#if RAVEN_ENABLE_DIAGNOSTICS == 0
        RAVEN_LOG_INFO(DiagnosticCategory::Core, 1u, std::int64_t(++evaluated));
        RAVEN_LOG_WARN(DiagnosticCategory::Core, 1u, std::int64_t(++evaluated));
        RAVEN_LOG_ERROR(DiagnosticCategory::Core, 1u, std::int64_t(++evaluated));
        if (IsDiagnosticsEnabled() == true || StartAsyncLogging() == true)
        {
            throw std::runtime_error("disabled logger is active");
        }
#else
        if (IsDiagnosticsEnabled() == false)
        {
            throw std::runtime_error("measurement-only disable removed logging");
        }
#endif
        SetMeasurementsEnabled(true);
        RAVEN_COUNTER("disabled", ++evaluated);
        RAVEN_TRACE_SCOPE((++evaluated, "disabled"));
        if (evaluated != 0 || IsMeasurementsEnabled() == true)
        {
            throw std::runtime_error("disabled diagnostics evaluated payload");
        }
#if RAVEN_ENABLE_DIAGNOSTICS
        {
            DiagnosticSession session;
            DiagnosticMetric metric;
            metric.SetName("disabled low-level API");
            if (session.IsActive() == false || EmitMetric(metric, DiagnosticCategory::Runtime, GetDiagnosticSessionId()) == true)
            {
                throw std::runtime_error("disabled measurement low-level API published an event");
            }
        }
#endif
        std::cout << "Diagnostics disabled: suites=1 failures=0\n";
#endif
        // 単純ループとの差で無効化時の概算コストを記録します。値は性能保証ではありません。
        constexpr int count = 1000000;
        volatile int baseline = 0;
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < count; ++i) { baseline = i; }
        const auto middle = std::chrono::steady_clock::now();
        Raven::Diagnostics::SetDiagnosticsEnabled(false);
        for (int i = 0; i < count; ++i)
        {
            baseline = i;
            RAVEN_LOG_INFO(Raven::Diagnostics::DiagnosticCategory::Core, 1u, std::int64_t(i));
        }
        const auto end = std::chrono::steady_clock::now();
        const double baselineNs = std::chrono::duration<double, std::nano>(middle - begin).count() / count;
        const double disabledNs = std::chrono::duration<double, std::nano>(end - middle).count() / count;
        std::cout << "disabled_ns_per_call=" << disabledNs << " baseline_ns=" << baselineNs << '\n';
        (void)baseline;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Diagnostics FAILED: " << error.what() << '\n';
        return 1;
    }
}
'@
Set-Content -LiteralPath (Join-Path $output 'main.cpp') -Value $main -Encoding utf8
$sources = Get-ChildItem (Join-Path $repo 'Root\Root\Raven\Diagnostics') -Recurse -Filter '*.cpp' |
    Where-Object { $Configuration -in @('Debug', 'Release') -or $_.Directory.Name -ne 'Tests' } |
    ForEach-Object { '"' + $_.FullName + '"' }
$sources += '"' + (Join-Path $repo 'Root\Root\Raven\Core\CPUProfiler.cpp') + '"'
$flags = '/nologo /std:c++20 /utf-8 /EHsc /W4 /DNOMINMAX /D_ENABLE_EXTENDED_ALIGNED_STORAGE'
if ($Configuration -eq 'Debug') { $flags += ' /Od /MDd /D_DEBUG' }
else { $flags += ' /O2 /MD /DNDEBUG' }
if ($Configuration -eq 'Disabled') { $flags += ' /DRAVEN_ENABLE_DIAGNOSTICS=0' }
if ($Configuration -eq 'MeasurementsDisabled') { $flags += ' /DRAVEN_ENABLE_DIAGNOSTIC_MEASUREMENTS=0' }
$buildLog = Join-Path $output 'build.log'
$testLog = Join-Path $output 'tests.log'
$batch = @(
    '@echo off'
    "call `"$vcvars`" >nul"
    'if errorlevel 1 exit /b %errorlevel%'
    "cl $flags /I`"$repo\Root\Root`" /Fo`"$output\\`" /Fe`"$output\DiagnosticsTests.exe`" $($sources -join ' ') `"$output\main.cpp`" > `"$buildLog`" 2>&1"
    'if errorlevel 1 exit /b %errorlevel%'
    "`"$output\DiagnosticsTests.exe`" > `"$testLog`" 2>&1"
    'exit /b %errorlevel%'
)
$batchPath = Join-Path $output 'run.cmd'
Set-Content -LiteralPath $batchPath -Value $batch -Encoding oem
Set-Content -LiteralPath $testLog -Value ''
Push-Location $repo
try {
    & cmd /c $batchPath
    $result = $LASTEXITCODE
    $warnings = @(Select-String -LiteralPath $buildLog -Pattern 'warning C\d+|warning LNK\d+').Count
    Write-Output "Diagnostics MSVC $Configuration`: exit=$result warnings=$warnings"
    if ($result -ne 0) {
        Select-String -LiteralPath $buildLog -Pattern 'error|fatal|warning' | Select-Object -First 20
    }
    Get-Content -LiteralPath $testLog -Tail 10
    if ($result -ne 0) { throw "Diagnostics verification failed. Logs: $output" }
}
finally { Pop-Location }
