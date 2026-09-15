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
    const UniformTemperatureField defaultField{};
    assert(NearlyEqual(defaultField.Evaluate(math::Vec3{}), 293.15f));
    assert(NearlyEqual(defaultField.EvaluateInfluence(math::Vec3{ 100.0f, -20.0f, 3.0f }), 1.0f));

    UniformTemperatureField heatedField{ 350.0f };
    assert(NearlyEqual(heatedField.GetTemperatureKelvin(), 350.0f));
    heatedField.SetTemperatureKelvin(-10.0f);
    assert(NearlyEqual(heatedField.GetTemperatureKelvin(), 0.0f));

    const GradientTemperatureField gradientField{
        math::Vec3{ 10.0f, 20.0f, 30.0f }, 300.0f, math::Vec3{ 2.0f, -5.0f, 1.0f }
    };
    assert(NearlyEqual(gradientField.Evaluate(math::Vec3{ 10.0f, 20.0f, 30.0f }), 300.0f));
    assert(NearlyEqual(gradientField.Evaluate(math::Vec3{ 12.0f, 21.0f, 33.0f }), 302.0f));

    const GradientTemperatureField coldGradientField{
        math::Vec3{}, 10.0f, math::Vec3{ -20.0f, 0.0f, 0.0f }
    };
    assert(NearlyEqual(coldGradientField.Evaluate(math::Vec3{ 1.0f, 0.0f, 0.0f }), 0.0f));

    // Hard RegionはCore内部だけInfluence=1となり、外側では温度値を持っていてもRegistry合成対象になりません。
    SphericalTemperatureRegionField hardRegion{
        math::Vec3{ 5.0f, 0.0f, 0.0f }, 2.0f, 400.0f
    };
    assert(NearlyEqual(hardRegion.Evaluate(math::Vec3{ 100.0f, 0.0f, 0.0f }), 400.0f));
    assert(NearlyEqual(hardRegion.EvaluateInfluence(math::Vec3{ 5.0f, 0.0f, 0.0f }), 1.0f));
    assert(NearlyEqual(hardRegion.EvaluateInfluence(math::Vec3{ 7.0f, 0.0f, 0.0f }), 1.0f));
    assert(NearlyEqual(hardRegion.EvaluateInfluence(math::Vec3{ 7.01f, 0.0f, 0.0f }), 0.0f));

    // Linear FalloffはRadius外側の指定距離で1->0へ線形減衰します。
    SphericalTemperatureRegionField linearRegion{
        math::Vec3{}, 2.0f, 400.0f, 2.0f, TemperatureRegionFalloff::Linear
    };
    assert(NearlyEqual(linearRegion.EvaluateInfluence(math::Vec3{ 2.0f, 0.0f, 0.0f }), 1.0f));
    assert(NearlyEqual(linearRegion.EvaluateInfluence(math::Vec3{ 3.0f, 0.0f, 0.0f }), 0.5f));
    assert(NearlyEqual(linearRegion.EvaluateInfluence(math::Vec3{ 4.0f, 0.0f, 0.0f }), 0.0f));

    // SmoothStepは中点ではLinearと同じ0.5ですが、端点付近の変化率を滑らかにします。
    linearRegion.SetFalloff(TemperatureRegionFalloff::SmoothStep);
    assert(NearlyEqual(linearRegion.EvaluateInfluence(math::Vec3{ 3.0f, 0.0f, 0.0f }), 0.5f));
    assert(linearRegion.EvaluateInfluence(math::Vec3{ 2.5f, 0.0f, 0.0f }) > 0.75f);

    linearRegion.SetRadius(-1.0f);
    linearRegion.SetFalloffDistance(-1.0f);
    linearRegion.SetInsideTemperatureKelvin(-50.0f);
    assert(NearlyEqual(linearRegion.GetRadius(), 0.0f));
    assert(NearlyEqual(linearRegion.GetFalloffDistance(), 0.0f));
    assert(NearlyEqual(linearRegion.GetInsideTemperatureKelvin(), 0.0f));

    ThermalWorld world{};
    TemperatureFieldRegistry& registry = world.GetTemperatureFieldRegistry();
    assert(NearlyEqual(registry.Evaluate(math::Vec3{}, 310.0f), 310.0f));

    UniformTemperatureField globalField{ 300.0f };
    SphericalTemperatureRegionField localField{
        math::Vec3{}, 1.0f, 400.0f, 2.0f, TemperatureRegionFalloff::Linear
    };
    assert(registry.RegisterField(globalField) == true);
    assert(registry.RegisterField(globalField) == false);
    assert(registry.RegisterField(localField) == true);

    // CoreではGlobal(Weight=1)とLocal(Weight=1)を平均します。
    assert(NearlyEqual(registry.Evaluate(math::Vec3{}, 280.0f), 350.0f));
    // Falloff中はLocal Weightだけ減少します。x=2ではLocal=0.5なので (300+400*0.5)/1.5 = 333.333...Kです。
    assert(NearlyEqual(registry.Evaluate(math::Vec3{ 2.0f, 0.0f, 0.0f }, 280.0f), 333.33334f, 1.0e-4f));
    // Region外ではLocalが平均対象から外れ、Global Fieldだけが残ります。
    assert(NearlyEqual(registry.Evaluate(math::Vec3{ 4.0f, 0.0f, 0.0f }, 280.0f), 300.0f));

    registry.Clear();
    SphericalTemperatureRegionField isolatedRegion{ math::Vec3{}, 1.0f, 400.0f };
    assert(registry.RegisterField(isolatedRegion) == true);
    // 登録Fieldが存在しても、その位置で全Influenceが0ならComponent側fallbackへ戻ります。
    assert(NearlyEqual(registry.Evaluate(math::Vec3{ 10.0f, 0.0f, 0.0f }, 280.0f), 280.0f));

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
