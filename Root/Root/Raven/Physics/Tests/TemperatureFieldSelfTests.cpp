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
    assert(NearlyEqual(defaultField.Evaluate(math::Vec3{}), 293.15f));
    assert(NearlyEqual(defaultField.EvaluateInfluence(math::Vec3{ 100.0f, -20.0f, 3.0f }), 1.0f));
    assert(defaultField.GetBlendMode() == TemperatureFieldBlendMode::WeightedAverage);
    assert(defaultField.GetPriority() == 0);

    // TemperatureFieldは環境温度をKelvinで表し、設定値を位置非依存で保持します。
    UniformTemperatureField heatedField{ 350.0f };
    assert(NearlyEqual(heatedField.GetTemperatureKelvin(), 350.0f));
    assert(NearlyEqual(heatedField.Evaluate(math::Vec3{ -4.0f, 8.0f, 2.0f }), 350.0f));

    // Kelvinの物理範囲をField境界で守り、負の絶対温度は0KへClampします。
    heatedField.SetTemperatureKelvin(-10.0f);
    assert(NearlyEqual(heatedField.GetTemperatureKelvin(), 0.0f));
    assert(NearlyEqual(heatedField.Evaluate(math::Vec3{}), 0.0f));

    // Gradient Fieldは基準位置からの変位と温度勾配の内積でworld-space温度を評価します。
    const GradientTemperatureField gradientField{ math::Vec3{ 10.0f, 20.0f, 30.0f }, 300.0f, math::Vec3{ 2.0f, -5.0f, 1.0f } };
    assert(NearlyEqual(gradientField.Evaluate(math::Vec3{ 12.0f, 21.0f, 33.0f }), 302.0f));
    const GradientTemperatureField coldGradientField{ math::Vec3{}, 10.0f, math::Vec3{ -20.0f, 0.0f, 0.0f } };
    assert(NearlyEqual(coldGradientField.Evaluate(math::Vec3{ 1.0f, 0.0f, 0.0f }), 0.0f));

    // Hard Sphere RegionはCore境界を含み、その直外ではInfluenceが0になります。
    SphericalTemperatureRegionField hardRegion{ math::Vec3{ 5.0f, 0.0f, 0.0f }, 2.0f, 400.0f };
    assert(NearlyEqual(hardRegion.EvaluateInfluence(math::Vec3{ 7.0f, 0.0f, 0.0f }), 1.0f));
    assert(NearlyEqual(hardRegion.EvaluateInfluence(math::Vec3{ 7.01f, 0.0f, 0.0f }), 0.0f));

    // Linear/SmoothStep Falloffは温度値ではなく位置依存Influenceを変化させます。
    SphericalTemperatureRegionField linearRegion{ math::Vec3{}, 2.0f, 400.0f, 2.0f, TemperatureRegionFalloff::Linear };
    assert(NearlyEqual(linearRegion.EvaluateInfluence(math::Vec3{ 3.0f, 0.0f, 0.0f }), 0.5f));
    linearRegion.SetFalloff(TemperatureRegionFalloff::SmoothStep);
    assert(NearlyEqual(linearRegion.EvaluateInfluence(math::Vec3{ 3.0f, 0.0f, 0.0f }), 0.5f));
    assert(linearRegion.EvaluateInfluence(math::Vec3{ 2.5f, 0.0f, 0.0f }) > 0.75f);

    // Box RegionはCore内/表面でInfluence=1、表面からFalloffDistanceまでEuclidean距離で減衰します。
    BoxTemperatureRegionField boxRegion{
        math::Vec3{ 10.0f, 0.0f, 0.0f }, math::Vec3{ 2.0f, 1.0f, 3.0f }, 360.0f,
        2.0f, TemperatureRegionFalloff::Linear
    };
    assert(NearlyEqual(boxRegion.EvaluateInfluence(math::Vec3{ 10.0f, 0.0f, 0.0f }), 1.0f));
    assert(NearlyEqual(boxRegion.EvaluateInfluence(math::Vec3{ 12.0f, 1.0f, 3.0f }), 1.0f));
    assert(NearlyEqual(boxRegion.EvaluateInfluence(math::Vec3{ 13.0f, 0.0f, 0.0f }), 0.5f));
    assert(NearlyEqual(boxRegion.EvaluateInfluence(math::Vec3{ 14.0f, 0.0f, 0.0f }), 0.0f));
    // Corner外側でも軸ごとの独立減衰ではなく、Box表面への最短Euclidean距離を使用します。
    const float cornerInfluence = boxRegion.EvaluateInfluence(math::Vec3{ 12.5f, 1.5f, 3.0f });
    assert(cornerInfluence > 0.0f && cornerInfluence < 1.0f);

    // 負のHalfExtents/温度/FalloffはField境界で物理的に有効な範囲へClampします。
    boxRegion.SetHalfExtents(math::Vec3{ -2.0f, 1.0f, -3.0f });
    boxRegion.SetInsideTemperatureKelvin(-10.0f);
    boxRegion.SetFalloffDistance(-1.0f);
    assert(NearlyEqual(boxRegion.GetHalfExtents().x, 0.0f));
    assert(NearlyEqual(boxRegion.GetHalfExtents().y, 1.0f));
    assert(NearlyEqual(boxRegion.GetHalfExtents().z, 0.0f));
    assert(NearlyEqual(boxRegion.GetInsideTemperatureKelvin(), 0.0f));
    assert(NearlyEqual(boxRegion.GetFalloffDistance(), 0.0f));

    // Registry未登録時は既存AmbientTemperatureをそのままfallbackとして利用します。
    ThermalWorld world{};
    TemperatureFieldRegistry& registry = world.GetTemperatureFieldRegistry();
    assert(NearlyEqual(registry.Evaluate(math::Vec3{}, 310.0f), 310.0f));

    // WeightedAverageではGlobal Fieldと局所FieldをInfluence加重平均し、領域外Fieldを自然に除外します。
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

    // 局所Fieldだけを登録した場合、領域外ではComponent側fallbackへ戻ります。
    registry.Clear();
    BoxTemperatureRegionField isolatedBox{ math::Vec3{}, math::Vec3{ 1.0f, 1.0f, 1.0f }, 420.0f };
    assert(registry.RegisterField(isolatedBox) == true);
    assert(NearlyEqual(registry.Evaluate(math::Vec3{}, 280.0f), 420.0f));
    assert(NearlyEqual(registry.Evaluate(math::Vec3{ 2.0f, 0.0f, 0.0f }, 280.0f), 280.0f));

    // ThermalWorld::Clear()はECS由来のTransient Body/Contactだけを破棄し、外部Field Registryは維持します。
    world.Clear();
    assert(registry.GetRegisteredFieldCount() == 1u);
    registry.Clear();

    // Fieldの評価値は既存ThermalEnvironmentContactへKelvinのまま渡せます。
    // ScalarFieldとThermal Solverの単位・境界条件が一致することを固定します。
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

    // ECS Temperature VolumeはTransient Fieldとして毎Fixed Step再構築します。
    // Persistent Fieldは同時に維持されるため、Scene同期で外部Fieldを失わないことも確認します。
    Scene volumeScene{};
    ThermalWorld& volumeWorld = volumeScene.GetPhysicsSimulationWorld().GetThermalWorld();
    TemperatureFieldRegistry& volumeRegistry = volumeWorld.GetTemperatureFieldRegistry();
    UniformTemperatureField persistentField{ 300.0f };
    assert(volumeRegistry.RegisterField(persistentField) == true);

    Entity probeEntity = volumeScene.CreateEntity("TemperatureVolumeProbe");
    probeEntity.GetComponent<TransformComponent>().Position = { 10.0f, 0.0f, 0.0f };
    probeEntity.AddComponent<ThermalBodyComponent>();
    ThermalConvectionComponent& probeConvection = probeEntity.AddComponent<ThermalConvectionComponent>();
    probeConvection.AmbientTemperature = 280.0f;

    Entity sphereVolumeEntity = volumeScene.CreateEntity("SphereTemperatureVolume");
    sphereVolumeEntity.GetComponent<TransformComponent>().Position = { 10.0f, 0.0f, 0.0f };
    SphericalTemperatureVolumeComponent& sphereVolume = sphereVolumeEntity.AddComponent<SphericalTemperatureVolumeComponent>();
    sphereVolume.Field.SetRadius(2.0f);
    sphereVolume.Field.SetInsideTemperatureKelvin(400.0f);
    sphereVolume.Field.SetBlendMode(TemperatureFieldBlendMode::Override);
    sphereVolume.Field.SetPriority(10);

    ThermalSystem::SynchronizeWorld(volumeScene);
    assert(volumeRegistry.GetPersistentFieldCount() == 1u);
    assert(volumeRegistry.GetTransientFieldCount() == 1u);
    assert(NearlyEqual(sphereVolume.Field.GetCenter().x, 10.0f));
    assert(NearlyEqual(volumeWorld.GetEnvironmentContacts().front().AmbientTemperature, 400.0f));

    // Transform移動後はField Centerも追従し、古い位置のProbeはPersistent環境へ戻ります。
    sphereVolumeEntity.GetComponent<TransformComponent>().Position = { 20.0f, 0.0f, 0.0f };
    ThermalSystem::SynchronizeWorld(volumeScene);
    assert(volumeRegistry.GetPersistentFieldCount() == 1u);
    assert(volumeRegistry.GetTransientFieldCount() == 1u);
    assert(NearlyEqual(sphereVolume.Field.GetCenter().x, 20.0f));
    assert(NearlyEqual(volumeWorld.GetEnvironmentContacts().front().AmbientTemperature, 300.0f));

    // Disabled Volumeは次同期でTransient Registryから外れます。
    sphereVolume.Enabled = false;
    ThermalSystem::SynchronizeWorld(volumeScene);
    assert(volumeRegistry.GetTransientFieldCount() == 0u);
    assert(volumeRegistry.ContainsField(persistentField) == true);

    // Box Volumeも同じTransient契約を使用し、Axis-Aligned FieldのCenterだけをTransformへ同期します。
    Entity boxVolumeEntity = volumeScene.CreateEntity("BoxTemperatureVolume");
    boxVolumeEntity.GetComponent<TransformComponent>().Position = { 10.0f, 0.0f, 0.0f };
    BoxTemperatureVolumeComponent& boxVolume = boxVolumeEntity.AddComponent<BoxTemperatureVolumeComponent>();
    boxVolume.Field.SetHalfExtents(math::Vec3{ 1.0f, 2.0f, 3.0f });
    boxVolume.Field.SetInsideTemperatureKelvin(420.0f);
    boxVolume.Field.SetBlendMode(TemperatureFieldBlendMode::Override);
    boxVolume.Field.SetPriority(20);
    ThermalSystem::SynchronizeWorld(volumeScene);
    assert(volumeRegistry.GetTransientFieldCount() == 1u);
    assert(NearlyEqual(boxVolume.Field.GetCenter().x, 10.0f));
    assert(NearlyEqual(volumeWorld.GetEnvironmentContacts().front().AmbientTemperature, 420.0f));

    // Entity破棄後も前StepのComponent pointerを保持せず、次同期でTransient登録が消えることを確認します。
    volumeScene.DestroyEntity(boxVolumeEntity);
    ThermalSystem::SynchronizeWorld(volumeScene);
    assert(volumeRegistry.GetPersistentFieldCount() == 1u);
    assert(volumeRegistry.GetTransientFieldCount() == 0u);
    assert(NearlyEqual(volumeWorld.GetEnvironmentContacts().front().AmbientTemperature, 300.0f));
}

} // namespace Raven::ph::tests
