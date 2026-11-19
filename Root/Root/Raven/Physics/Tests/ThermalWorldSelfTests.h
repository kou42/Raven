#pragma once

namespace Raven::ph::tests
{

// Thermal Solver / ECS Registry / Rigid Contact熱連成の基礎回帰テストです。
void RunThermalWorldSelfTests();
// 明示CLIからDebug / Releaseの両方で実行する。
bool RunGridThermalSelfTests();

} // namespace Raven::ph::tests
