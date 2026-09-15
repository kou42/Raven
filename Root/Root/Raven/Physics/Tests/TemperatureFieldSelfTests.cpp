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

    // Fieldの評価値は既存ThermalEnvironmentContactへKelvinのまま渡せます。
    // Runtime Registry統合前に、ScalarFieldとThermal Solverの単位・境界条件が一致することを固定します。
    ThermalBody body{};
    body.Temperature = 300.0f;
    body.Material.SpecificHeatCapacity = 100.0f;

    ThermalWorld world{};
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
