#include "Raven/Physics/Thermal/ThermalComponents.h"
#include "Raven/Physics/Thermal/ThermalSystem.h"
#include "Raven/Physics/Thermal/ThermalWorld.h"
#include "Raven/Scene/Scene.h"

#include <cassert>
#include <cmath>

namespace Raven::ph::tests
{
namespace
{
float CalculateThermalEnergy(const ThermalBody& body)
{
    return body.GetHeatCapacity() * body.Temperature;
}

void RunThermalConductionTest()
{
    ThermalBody hotBody{};
    hotBody.Temperature = 373.15f;
    hotBody.Mass = 1.0f;
    hotBody.Material.SpecificHeatCapacity = 500.0f;
    hotBody.Material.ThermalConductivity = 50.0f;

    ThermalBody coldBody{};
    coldBody.Temperature = 293.15f;
    coldBody.Mass = 1.0f;
    coldBody.Material.SpecificHeatCapacity = 500.0f;
    coldBody.Material.ThermalConductivity = 50.0f;

    ThermalWorld world{};
    assert(world.RegisterBody(hotBody) == true);
    assert(world.RegisterBody(coldBody) == true);
    assert(world.RegisterBody(hotBody) == false);

    ThermalContact contact{};
    contact.BodyA = &hotBody;
    contact.BodyB = &coldBody;
    contact.ContactArea = 0.01f;
    contact.ConductionDistance = 0.01f;
    assert(world.RegisterContact(contact) == true);

    const float initialEnergy =
        CalculateThermalEnergy(hotBody) + CalculateThermalEnergy(coldBody);

    world.Step(1.0f);

    const float finalEnergy =
        CalculateThermalEnergy(hotBody) + CalculateThermalEnergy(coldBody);

    assert(hotBody.Temperature < 373.15f);
    assert(coldBody.Temperature > 293.15f);
    assert(std::abs(finalEnergy - initialEnergy) < 1.0f);

    // 十分大きいStepでも平衡温度を飛び越えないことを確認します。
    world.Step(100000.0f);
    assert(std::abs(hotBody.Temperature - 333.15f) < 1.0e-3f);
    assert(std::abs(coldBody.Temperature - 333.15f) < 1.0e-3f);
}

void RunThermalEcsSynchronizationTest()
{
    Scene scene{};
    Entity hotEntity = scene.CreateEntity("ThermalHot");
    Entity coldEntity = scene.CreateEntity("ThermalCold");

    // ComponentStorageはdense vectorなので、2件目の追加で1件目の参照が無効化される可能性があります。
    // すべて追加してからGetComponent()で参照を取得し直します。
    hotEntity.AddComponent<ThermalBodyComponent>();
    coldEntity.AddComponent<ThermalBodyComponent>();

    ThermalBodyComponent& hotComponent = hotEntity.GetComponent<ThermalBodyComponent>();
    ThermalBodyComponent& coldComponent = coldEntity.GetComponent<ThermalBodyComponent>();
    hotComponent.Body.Temperature = 373.15f;
    coldComponent.Body.Temperature = 293.15f;

    ThermalContactComponent& contactComponent =
        hotEntity.AddComponent<ThermalContactComponent>();
    contactComponent.TargetEntity = coldEntity.GetHandle();
    contactComponent.ContactArea = 0.01f;
    contactComponent.ConductionDistance = 0.01f;

    ThermalSystem::SynchronizeWorld(scene);

    ThermalWorld& world = scene.GetPhysicsSimulationWorld().GetThermalWorld();
    assert(world.GetRegisteredBodyCount() == 2u);
    assert(world.GetContactCount() == 1u);

    world.Step(1.0f);
    assert(hotComponent.Body.Temperature < 373.15f);
    assert(coldComponent.Body.Temperature > 293.15f);

    // Target Entityを破棄した後は、次のRegistry再構築でContactもBodyも除外されます。
    // EntityHandleのGeneration検証により、同じIndexが再利用されても古いPairへ接続しません。
    scene.DestroyEntity(coldEntity);
    ThermalSystem::SynchronizeWorld(scene);
    assert(world.GetRegisteredBodyCount() == 1u);
    assert(world.GetContactCount() == 0u);
}
}

// Debuggerや既存Self Test runnerから呼び出すための基礎検証です。
// Solver単体の熱量保存に加え、ECS -> ThermalWorld Registry再構築のlifetime契約も確認します。
void RunThermalWorldSelfTests()
{
    RunThermalConductionTest();
    RunThermalEcsSynchronizationTest();
}

}
