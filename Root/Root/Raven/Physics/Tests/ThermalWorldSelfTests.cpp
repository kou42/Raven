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

    world.Step(100000.0f);
    assert(std::abs(hotBody.Temperature - 333.15f) < 1.0e-3f);
    assert(std::abs(coldBody.Temperature - 333.15f) < 1.0e-3f);
}

void RunThermalNetworkSubstepTest()
{
    ThermalBody hotBody{};
    ThermalBody centerBody{};
    ThermalBody coldBody{};

    hotBody.Temperature = 400.0f;
    centerBody.Temperature = 300.0f;
    coldBody.Temperature = 200.0f;

    // 小さい熱容量と大きいGを意図的に組み合わせ、1回のExplicit Eulerでは
    // 多接触の熱量和が大きくなりやすい条件を作ります。
    hotBody.Mass = 1.0f;
    centerBody.Mass = 1.0f;
    coldBody.Mass = 1.0f;
    hotBody.Material.SpecificHeatCapacity = 1.0f;
    centerBody.Material.SpecificHeatCapacity = 1.0f;
    coldBody.Material.SpecificHeatCapacity = 1.0f;

    ThermalWorld world{};
    assert(world.RegisterBody(hotBody) == true);
    assert(world.RegisterBody(centerBody) == true);
    assert(world.RegisterBody(coldBody) == true);

    ThermalContact hotToCenter{};
    hotToCenter.BodyA = &hotBody;
    hotToCenter.BodyB = &centerBody;
    hotToCenter.ThermalConductance = 10.0f;
    assert(world.RegisterContact(hotToCenter) == true);

    ThermalContact centerToCold{};
    centerToCold.BodyA = &centerBody;
    centerToCold.BodyB = &coldBody;
    centerToCold.ThermalConductance = 10.0f;
    assert(world.RegisterContact(centerToCold) == true);

    const float initialEnergy =
        CalculateThermalEnergy(hotBody)
        + CalculateThermalEnergy(centerBody)
        + CalculateThermalEnergy(coldBody);

    world.Step(1.0f);

    const float finalEnergy =
        CalculateThermalEnergy(hotBody)
        + CalculateThermalEnergy(centerBody)
        + CalculateThermalEnergy(coldBody);

    // centerはsum(G)=20W/K、C=1J/Kなのでtau=0.05sです。
    // SafetyFactor=0.5から要求dt<=0.025sとなり、1秒Stepは約40 substepへ分割されます。
    // float丸めでceil結果が1増える可能性があるため40/41の両方を許容します。
    assert(world.GetLastSubstepCount() >= 40u);
    assert(world.GetLastSubstepCount() <= 41u);
    assert(std::abs(finalEnergy - initialEnergy) < 1.0e-3f);

    // 対称な3 Body chainなので中心温度は300Kを維持し、両端は中心へ単調に近づきます。
    assert(hotBody.Temperature < 400.0f);
    assert(hotBody.Temperature >= 300.0f);
    assert(std::abs(centerBody.Temperature - 300.0f) < 1.0e-3f);
    assert(coldBody.Temperature > 200.0f);
    assert(coldBody.Temperature <= 300.0f);

    world.SetSubstepSafetyFactor(0.0f);
    world.SetMaximumSubsteps(0u);
    assert(std::abs(world.GetSubstepSafetyFactor() - 0.5f) < 1.0e-6f);
    assert(world.GetMaximumSubsteps() == 64u);
}

void RunThermalEcsSynchronizationTest()
{
    Scene scene{};
    Entity hotEntity = scene.CreateEntity("ThermalHot");
    Entity coldEntity = scene.CreateEntity("ThermalCold");

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
    assert(std::abs(generatedContact.ContactArea - 0.002f) < 1.0e-6f);
    assert(std::abs(generatedContact.ConductionDistance - 0.03f) < 1.0e-6f);
    assert(std::abs(generatedContact.ConductivityScale - 0.5f) < 1.0e-6f);
    assert(generatedContact.ThermalConductance > 0.0f);

    world.Step(1.0f);
    assert(hotComponent.Body.Temperature < 373.15f);
    assert(coldComponent.Body.Temperature > 293.15f);

    ThermalSystem::SynchronizeWorld(scene);
    manifolds.front().IsTrigger = true;
    ThermalSystem::AppendRigidBodyContacts(scene, manifolds);
    assert(world.GetContactCount() == 0u);

    coldSettings.Enabled = false;
    manifolds.front().IsTrigger = false;
    ThermalSystem::SynchronizeWorld(scene);
    ThermalSystem::AppendRigidBodyContacts(scene, manifolds);
    assert(world.GetContactCount() == 0u);
}

void RunPhysicsSimulationThermalContactTest()
{
    Scene scene{};
    Entity hotEntity = scene.CreateEntity("PhysicsThermalHot");
    Entity coldEntity = scene.CreateEntity("PhysicsThermalCold");

    TransformComponent& hotTransform = hotEntity.GetComponent<TransformComponent>();
    TransformComponent& coldTransform = coldEntity.GetComponent<TransformComponent>();
    hotTransform.Position = { 0.0f, 0.0f, 0.0f };
    coldTransform.Position = { 0.9f, 0.0f, 0.0f };

    RigidBodyComponent hotRigidBody{};
    hotRigidBody.UseGravity = false;
    hotRigidBody.AllowSleep = false;
    hotEntity.AddComponent<RigidBodyComponent>(hotRigidBody);

    RigidBodyComponent coldRigidBody{};
    coldRigidBody.UseGravity = false;
    coldRigidBody.AllowSleep = false;
    coldEntity.AddComponent<RigidBodyComponent>(coldRigidBody);

    ColliderComponent hotCollider{};
    hotCollider.Type = ColliderType::Sphere;
    hotCollider.Radius = 0.5f;
    hotEntity.AddComponent<ColliderComponent>(hotCollider);

    ColliderComponent coldCollider{};
    coldCollider.Type = ColliderType::Sphere;
    coldCollider.Radius = 0.5f;
    coldEntity.AddComponent<ColliderComponent>(coldCollider);

    hotEntity.AddComponent<ThermalBodyComponent>();
    coldEntity.AddComponent<ThermalBodyComponent>();
    hotEntity.AddComponent<ThermalRigidContactComponent>();
    coldEntity.AddComponent<ThermalRigidContactComponent>();

    ThermalBodyComponent& hotThermal = hotEntity.GetComponent<ThermalBodyComponent>();
    ThermalBodyComponent& coldThermal = coldEntity.GetComponent<ThermalBodyComponent>();
    hotThermal.Body.Temperature = 373.15f;
    coldThermal.Body.Temperature = 293.15f;

    ThermalRigidContactComponent& hotSettings =
        hotEntity.GetComponent<ThermalRigidContactComponent>();
    ThermalRigidContactComponent& coldSettings =
        coldEntity.GetComponent<ThermalRigidContactComponent>();
    hotSettings.NominalContactAreaPerPoint = 0.01f;
    coldSettings.NominalContactAreaPerPoint = 0.01f;
    hotSettings.ConductionDistance = 0.01f;
    coldSettings.ConductionDistance = 0.01f;

    constexpr float fixedDeltaTime = 1.0f / 60.0f;
    PhysicsSimulationWorld& simulationWorld = scene.GetPhysicsSimulationWorld();
    simulationWorld.StepSimulation(scene, fixedDeltaTime);

    assert(simulationWorld.GetRigidBodyWorld().GetContactManifolds().empty() == false);
    assert(simulationWorld.GetThermalWorld().GetContactCount() == 1u);
    assert(hotThermal.Body.Temperature < 373.15f);
    assert(coldThermal.Body.Temperature > 293.15f);
}
}

void RunThermalWorldSelfTests()
{
    RunThermalConductionTest();
    RunThermalNetworkSubstepTest();
    RunThermalEcsSynchronizationTest();
    RunRigidContactThermalCouplingTest();
    RunPhysicsSimulationThermalContactTest();
}

}
