#include "Raven/Physics/Tests/PhysicsFieldSelfTests.h"

#include "Raven/Physics/Tests/ElectricPotentialFieldSelfTests.h"
#include "Raven/Physics/Tests/TemperatureFieldSelfTests.h"

namespace Raven::ph::tests
{

void RunPhysicsFieldSelfTests()
{
    // ScalarFieldの具体型を同じ入口へ集約し、Field追加時のStartup接続漏れを防ぎます。
    RunElectricPotentialFieldSelfTests();
    RunTemperatureFieldSelfTests();
}

} // namespace Raven::ph::tests
