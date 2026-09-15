#include "Raven/Physics/Tests/TemperatureFieldSelfTests.h"

#include <cassert>
#include <cmath>

#include "Raven/Physics/Thermal/TemperatureField.h"
#include "Raven/Physics/Thermal/ThermalComponents.h"
#include "Raven/Physics/Thermal/ThermalSystem.h"
#include "Raven/Physics/Thermal/ThermalWorld.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph::tests
{
namespace
{

bool NearlyEqual(float lhs, float rhs, float epsilon = 1.0e-5f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

// ECS Runtimeが固定値ではなくEntityのworld-space位置をFieldへ渡していることを確認するTest用Fieldです。
class PositionTemperatureField final : public TemperatureField
{
public:
    float Evaluate(const math::Vec3& worldPosition) const override
    {
        return 300.0f + worldPosition.x * 10.0f;
    }
};

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

    // ThermalSystemはConvection EntityのTransform位置でTemperatureFieldを評価します。
    // Component側のAmbientTemperatureはField未登録時のfallbackであり、Field登録時は空間温度が優先されます。
    Scene scene{};
    Entity thermalEntity = scene.CreateEntity("TemperatureFieldRuntimeTest");
    thermalEntity.GetComponent<TransformComponent>().Position = { 4.0f, 2.0f, -1.0f };
    thermalEntity.AddComponent<ThermalBodyComponent>();
    ThermalConvectionComponent& convection = thermalEntity.AddComponent<ThermalConvectionComponent>();
    convection.AmbientTemperature = 280.0f;

    ThermalWorld& sceneThermalWorld = scene.GetPhysicsSimulationWorld().GetThermalWorld();
    PositionTemperatureField positionField{};
    assert(sceneThermalWorld.GetTemperatureFieldRegistry().RegisterField(positionField) == true);

    ThermalSystem::SynchronizeWorld(scene);
    assert(sceneThermalWorld.GetEnvironmentContactCount() == 1u);
    assert(NearlyEqual(sceneThermalWorld.GetEnvironmentContacts().front().AmbientTemperature, 340.0f));

    // Fieldを外すと既存Component値へ戻り、従来Sceneとの後方互換性を維持します。
    assert(sceneThermalWorld.GetTemperatureFieldRegistry().UnregisterField(positionField) == true);
    ThermalSystem::SynchronizeWorld(scene);
    assert(NearlyEqual(sceneThermalWorld.GetEnvironmentContacts().front().AmbientTemperature, 280.0f));
}

} // namespace Raven::ph::tests
