#include "Raven/Physics/Tests/TemperatureFieldSelfTests.h"

#include <cassert>
#include <cmath>

#include "Raven/Physics/Thermal/TemperatureField.h"
#include "Raven/Physics/Thermal/ThermalWorld.h"

namespace Raven::ph::tests
{
namespace
{

bool NearlyEqual(float lhs, float rhs, float epsilon = 1.0e-5f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

void RunTemperatureFieldSelfTests()
{
    // UniformTemperatureFieldは空間位置に依存せず、既定の室温293.15Kを返します。
    const UniformTemperatureField defaultField{};
    assert(NearlyEqual(defaultField.Evaluate(math::Vec3{ 0.0f, 0.0f, 0.0f }), 293.15f));
    assert(NearlyEqual(defaultField.Evaluate(math::Vec3{ 100.0f, -20.0f, 3.0f }), 293.15f));

    // TemperatureFieldは環境温度をKelvinで表し、設定値を位置非依存で保持します。
    UniformTemperatureField heatedField{ 350.0f };
    assert(NearlyEqual(heatedField.GetTemperatureKelvin(), 350.0f));
    assert(NearlyEqual(heatedField.Evaluate(math::Vec3{ -4.0f, 8.0f, 2.0f }), 350.0f));

    // Kelvinの物理範囲をField境界で守り、負の絶対温度は0KへClampします。
    heatedField.SetTemperatureKelvin(-10.0f);
    assert(NearlyEqual(heatedField.GetTemperatureKelvin(), 0.0f));
    assert(NearlyEqual(heatedField.Evaluate(math::Vec3{}), 0.0f));

    // Registry未登録時は既存AmbientTemperatureをそのままfallbackとして利用します。
    ThermalWorld world{};
    TemperatureFieldRegistry& registry = world.GetTemperatureFieldRegistry();
    assert(registry.GetRegisteredFieldCount() == 0u);
    assert(NearlyEqual(registry.Evaluate(math::Vec3{}, 310.0f), 310.0f));

    UniformTemperatureField firstEnvironmentField{ 330.0f };
    UniformTemperatureField secondEnvironmentField{ 350.0f };
    assert(registry.RegisterField(firstEnvironmentField) == true);
    assert(registry.RegisterField(firstEnvironmentField) == false);
    assert(NearlyEqual(registry.Evaluate(math::Vec3{ 1.0f, 2.0f, 3.0f }, 310.0f), 330.0f));

    // 複数Fieldは現段階のBlend Policyとして平均し、単純加算による温度増幅を避けます。
    assert(registry.RegisterField(secondEnvironmentField) == true);
    assert(NearlyEqual(registry.Evaluate(math::Vec3{}, 310.0f), 340.0f));

    // ThermalWorld::Clear()はECS由来のTransient Body/Contactだけを破棄し、外部Field Registryは維持します。
    world.Clear();
    assert(registry.GetRegisteredFieldCount() == 2u);
    assert(registry.UnregisterField(firstEnvironmentField) == true);
    assert(registry.UnregisterField(firstEnvironmentField) == false);
    registry.Clear();
    assert(registry.GetRegisteredFieldCount() == 0u);

    // Fieldの評価値は既存ThermalEnvironmentContactへKelvinのまま渡せます。
    ThermalBody body{};
    body.Temperature = 300.0f;
    body.Material.SpecificHeatCapacity = 100.0f;
    assert(world.RegisterBody(body) == true);

    const UniformTemperatureField environmentField{ 350.0f };
    ThermalEnvironmentContact environment{};
    environment.Body = &body;
    environment.AmbientTemperature = environmentField.Evaluate(math::Vec3{ 4.0f, 5.0f, 6.0f });
    environment.ThermalConductance = 10.0f;
    assert(world.RegisterEnvironmentContact(environment) == true);

    world.Step(1.0f);
    assert(body.Temperature > 300.0f);
    assert(body.Temperature <= 350.0f);
}

} // namespace Raven::ph::tests
