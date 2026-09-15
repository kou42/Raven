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
bool NearlyEqual(float lhs, float rhs, float epsilon = 1.0e-5f) { return std::fabs(lhs - rhs) <= epsilon; }
class PositionTemperatureField final : public TemperatureField
{
public:
    float Evaluate(const math::Vec3& worldPosition) const override { return 300.0f + worldPosition.x * 10.0f; }
};
}

void RunTemperatureFieldSelfTests()
{
    const UniformTemperatureField defaultField{};
    assert(NearlyEqual(defaultField.Evaluate(math::Vec3{}), 293.15f));
    assert(NearlyEqual(defaultField.EvaluateInfluence(math::Vec3{ 100.0f, -20.0f, 3.0f }), 1.0f));
    assert(defaultField.GetBlendMode() == TemperatureFieldBlendMode::WeightedAverage);
    assert(defaultField.GetPriority() == 0);

    UniformTemperatureField heatedField{ 350.0f };
    heatedField.SetTemperatureKelvin(-10.0f);
    assert(NearlyEqual(heatedField.GetTemperatureKelvin(), 0.0f));

    const GradientTemperatureField gradientField{ math::Vec3{ 10.0f, 20.0f, 30.0f }, 300.0f, math::Vec3{ 2.0f, -5.0f, 1.0f } };
    assert(NearlyEqual(gradientField.Evaluate(math::Vec3{ 12.0f, 21.0f, 33.0f }), 302.0f));
    const GradientTemperatureField coldGradientField{ math::Vec3{}, 10.0f, math::Vec3{ -20.0f, 0.0f, 0.0f } };
    assert(NearlyEqual(coldGradientField.Evaluate(math::Vec3{ 1.0f, 0.0f, 0.0f }), 0.0f));

    SphericalTemperatureRegionField hardRegion{ math::Vec3{ 5.0f, 0.0f, 0.0f }, 2.0f, 400.0f };
    assert(NearlyEqual(hardRegion.EvaluateInfluence(math::Vec3{ 7.0f, 0.0f, 0.0f }), 1.0f));
    assert(NearlyEqual(hardRegion.EvaluateInfluence(math::Vec3{ 7.01f, 0.0f, 0.0f }), 0.0f));

    SphericalTemperatureRegionField linearRegion{ math::Vec3{}, 2.0f, 400.0f, 2.0f, TemperatureRegionFalloff::Linear };
    assert(NearlyEqual(linearRegion.EvaluateInfluence(math::Vec3{ 3.0f, 0.0f, 0.0f }), 0.5f));
    linearRegion.SetFalloff(TemperatureRegionFalloff::SmoothStep);
    assert(NearlyEqual(linearRegion.EvaluateInfluence(math::Vec3{ 3.0f, 0.0f, 0.0f }), 0.5f));
    assert(linearRegion.EvaluateInfluence(math::Vec3{ 2.5f, 0.0f, 0.0f }) > 0.75f);

    BoxTemperatureRegionField boxRegion{
        math::Vec3{ 10.0f, 0.0f, 0.0f }, math::Vec3{ 2.0f, 1.0f, 3.0f }, 360.0f,
        2.0f, TemperatureRegionFalloff::Linear
    };
    assert(NearlyEqual(boxRegion.EvaluateInfluence(math::Vec3{ 10.0f, 0.0f, 0.0f }), 1.0f));
    assert(NearlyEqual(boxRegion.EvaluateInfluence(math::Vec3{ 12.0f, 1.0f, 3.0f }), 1.0f));
    assert(NearlyEqual(boxRegion.EvaluateInfluence(math::Vec3{ 13.0f, 0.0f, 0.0f }), 0.5f));
    assert(NearlyEqual(boxRegion.EvaluateInfluence(math::Vec3{ 14.0f, 0.0f, 0.0f }), 0.0f));
    const float cornerInfluence = boxRegion.EvaluateInfluence(math::Vec3{ 12.5f, 1.5f, 3.0f });
    assert(cornerInfluence > 0.0f && cornerInfluence < 1.0f);

    boxRegion.SetHalfExtents(math::Vec3{ -2.0f, 1.0f, -3.0f });
    boxRegion.SetInsideTemperatureKelvin(-10.0f);
    boxRegion.SetFalloffDistance(-1.0f);
    assert(NearlyEqual(boxRegion.GetHalfExtents().x, 0.0f));
    assert(NearlyEqual(boxRegion.GetHalfExtents().y, 1.0f));
    assert(NearlyEqual(boxRegion.GetHalfExtents().z, 0.0f));
    assert(NearlyEqual(boxRegion.GetInsideTemperatureKelvin(), 0.0f));
    assert(NearlyEqual(boxRegion.GetFalloffDistance(), 0.0f));

    ThermalWorld world{};
    TemperatureFieldRegistry& registry = world.GetTemperatureFieldRegistry();
    assert(NearlyEqual(registry.Evaluate(math::Vec3{}, 310.0f), 310.0f));
    UniformTemperatureField globalField{ 300.0f };
    SphericalTemperatureRegionField localField{ math::Vec3{}, 1.0f, 400.0f, 2.0f, TemperatureRegionFalloff::Linear };
    assert(registry.RegisterField(globalField) == true);
    assert(registry.RegisterField(globalField) == false);
    assert(registry.RegisterField(localField) == true);
    assert(NearlyEqual(registry.Evaluate(math::Vec3{}, 280.0f), 350.0f));
    assert(NearlyEqual(registry.Evaluate(math::Vec3{ 2.0f, 0.0f, 0.0f }, 280.0f), 333.33334f, 1.0e-4f));
    assert(NearlyEqual(registry.Evaluate(math::Vec3{ 4.0f, 0.0f, 0.0f }, 280.0f), 300.0f));

    // OverrideはCoreで基礎環境を完全置換し、FalloffではInfluenceをAlphaとして基礎環境へ戻します。
    localField.SetBlendMode(TemperatureFieldBlendMode::Override);
    localField.SetPriority(10);
    assert(NearlyEqual(registry.Evaluate(math::Vec3{}, 280.0f), 400.0f));
    assert(NearlyEqual(registry.Evaluate(math::Vec3{ 2.0f, 0.0f, 0.0f }, 280.0f), 350.0f));
    assert(NearlyEqual(registry.Evaluate(math::Vec3{ 4.0f, 0.0f, 0.0f }, 280.0f), 300.0f));

    // 重なったOverrideでは最高Priorityだけが採用されます。
    SphericalTemperatureRegionField highPriorityField{ math::Vec3{}, 1.0f, 500.0f };
    highPriorityField.SetBlendMode(TemperatureFieldBlendMode::Override);
    highPriorityField.SetPriority(20);
    assert(registry.RegisterField(highPriorityField) == true);
    assert(NearlyEqual(registry.Evaluate(math::Vec3{}, 280.0f), 500.0f));

    // 同Priorityは登録順ではなくInfluence加重平均で決定します。Core同士なら400Kと600Kの平均500Kです。
    registry.Clear();
    SphericalTemperatureRegionField samePriorityA{ math::Vec3{}, 1.0f, 400.0f };
    SphericalTemperatureRegionField samePriorityB{ math::Vec3{}, 1.0f, 600.0f };
    samePriorityA.SetBlendMode(TemperatureFieldBlendMode::Override);
    samePriorityB.SetBlendMode(TemperatureFieldBlendMode::Override);
    samePriorityA.SetPriority(5);
    samePriorityB.SetPriority(5);
    assert(registry.RegisterField(samePriorityA) == true);
    assert(registry.RegisterField(samePriorityB) == true);
    assert(NearlyEqual(registry.Evaluate(math::Vec3{}, 280.0f), 500.0f));

    registry.Clear();
    BoxTemperatureRegionField isolatedBox{ math::Vec3{}, math::Vec3{ 1.0f, 1.0f, 1.0f }, 420.0f };
    assert(registry.RegisterField(isolatedBox) == true);
    assert(NearlyEqual(registry.Evaluate(math::Vec3{}, 280.0f), 420.0f));
    assert(NearlyEqual(registry.Evaluate(math::Vec3{ 2.0f, 0.0f, 0.0f }, 280.0f), 280.0f));
    world.Clear();
    assert(registry.GetRegisteredFieldCount() == 1u);
    registry.Clear();

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
    assert(sceneThermalWorld.GetTemperatureFieldRegistry().UnregisterField(positionField) == true);
    ThermalSystem::SynchronizeWorld(scene);
    assert(NearlyEqual(sceneThermalWorld.GetEnvironmentContacts().front().AmbientTemperature, 280.0f));
}

} // namespace Raven::ph::tests
