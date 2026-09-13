#include "Raven/Physics/Contact.h"
#include "Raven/Physics/Thermal/ThermalComponents.h"
#include "Raven/Physics/Thermal/ThermalSystem.h"
#include "Raven/Physics/Thermal/ThermalWorld.h"
#include "Raven/Scene/Scene.h"

#include <cassert>
#include <cmath>
#include <vector>

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

void RunRigidContactThermalCouplingTest()
{
    Scene scene{};
    Entity hotEntity = scene.CreateEntity("RigidThermalHot");
    Entity coldEntity = scene.CreateEntity("RigidThermalCold");

    hotEntity.AddComponent<ThermalBodyComponent>();
    coldEntity.AddComponent<ThermalBodyComponent>();
    hotEntity.AddComponent<ThermalRigidContactComponent>();
    coldEntity.AddComponent<ThermalRigidContactComponent>();

    ThermalBodyComponent& hotComponent = hotEntity.GetComponent<ThermalBodyComponent>();
    ThermalBodyComponent& coldComponent = coldEntity.GetComponent<ThermalBodyComponent>();
    hotComponent.Body.Temperature = 373.15f;
    coldComponent.Body.Temperature = 293.15f;

    ThermalRigidContactComponent& hotSettings =
        hotEntity.GetComponent<ThermalRigidContactComponent>();
    ThermalRigidContactComponent& coldSettings =
        coldEntity.GetComponent<ThermalRigidContactComponent>();
    hotSettings.NominalContactAreaPerPoint = 0.002f;
    coldSettings.NominalContactAreaPerPoint = 0.001f;
    hotSettings.ConductionDistance = 0.02f;
    coldSettings.ConductionDistance = 0.04f;
    hotSettings.ConductivityScale = 1.0f;
    coldSettings.ConductivityScale = 0.25f;

    ThermalSystem::SynchronizeWorld(scene);

    ContactManifold manifold{};
    manifold.A = hotEntity;
    manifold.B = coldEntity;
    manifold.AddPoint(ContactPoint{});
    manifold.AddPoint(ContactPoint{});

    std::vector<ContactManifold> manifolds{ manifold };
    ThermalSystem::AppendRigidBodyContacts(scene, manifolds);

    ThermalWorld& world = scene.GetPhysicsSimulationWorld().GetThermalWorld();
    assert(world.GetContactCount() == 1u);

    const ThermalContact& generatedContact = world.GetContacts().front();
    // 面積はPair双方の小さい設定値0.001m^2 x 2点、距離は平均0.03mです。
    // ConductivityScaleは幾何平均sqrt(1.0 * 0.25) = 0.5を使用します。
    assert(std::abs(generatedContact.ContactArea - 0.002f) < 1.0e-6f);
    assert(std::abs(generatedContact.ConductionDistance - 0.03f) < 1.0e-6f);
    assert(std::abs(generatedContact.ConductivityScale - 0.5f) < 1.0e-6f);

    world.Step(1.0f);
    assert(hotComponent.Body.Temperature < 373.15f);
    assert(coldComponent.Body.Temperature > 293.15f);

    // Triggerは物理的な接触面を意味しないため熱接触へ変換しません。
    ThermalSystem::SynchronizeWorld(scene);
    manifolds.front().IsTrigger = true;
    ThermalSystem::AppendRigidBodyContacts(scene, manifolds);
    assert(world.GetContactCount() == 0u);

    // Pairの片側がopt-in設定を無効化した場合も自動熱伝導を生成しません。
    coldSettings.Enabled = false;
    manifolds.front().IsTrigger = false;
    ThermalSystem::SynchronizeWorld(scene);
    ThermalSystem::AppendRigidBodyContacts(scene, manifolds);
    assert(world.GetContactCount() == 0u);
}
}

// Debuggerや既存Self Test runnerから呼び出すための基礎検証です。
// Solver単体、ECS Registry lifetime、Rigid Contact -> Thermal Contact変換を確認します。
void RunThermalWorldSelfTests()
{
    RunThermalConductionTest();
    RunThermalEcsSynchronizationTest();
    RunRigidContactThermalCouplingTest();
}

}
