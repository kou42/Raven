#pragma once

namespace Raven::tests
{
// GPUを起動せず、Console/Buffer/Collectorと保存・再読込を確認します。
// Releaseでもチェックを省略せず、失敗時は例外で専用Runnerへ通知します。
void RunDiagnosticSelfTests();
void RunDiagnosticSinkSelfTests();
}
