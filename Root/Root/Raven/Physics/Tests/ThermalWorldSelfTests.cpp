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
float CalculateThermalEnergy(const ThermalBody& body) { return body.GetHeatCapacity() * body.Temperature; }

void RunThermalConductionTest()
{
    ThermalBody hotBody{}; hotBody.Temperature = 373.15f; hotBody.Mass = 1.0f;
    hotBody.Material.SpecificHeatCapacity = 500.0f; hotBody.Material.ThermalConductivity = 50.0f;
    ThermalBody coldBody = hotBody; coldBody.Temperature = 293.15f;
    ThermalWorld world{}; assert(world.RegisterBody(hotBody) == true); assert(world.RegisterBody(coldBody) == true);
    ThermalContact contact{}; contact.BodyA = &hotBody; contact.BodyB = &coldBody; contact.ContactArea = 0.01f; contact.ConductionDistance = 0.01f;
    assert(world.RegisterContact(contact) == true);
    const float initialEnergy = CalculateThermalEnergy(hotBody) + CalculateThermalEnergy(coldBody);
    world.Step(1.0f);
    assert(hotBody.Temperature < 373.15f); assert(coldBody.Temperature > 293.15f);
    assert(std::abs(CalculateThermalEnergy(hotBody) + CalculateThermalEnergy(coldBody) - initialEnergy) < 1.0f);
    world.Step(100000.0f);
    assert(std::abs(hotBody.Temperature - 333.15f) < 1.0e-3f); assert(std::abs(coldBody.Temperature - 333.15f) < 1.0e-3f);
}

void RunThermalNetworkSubstepTest()
{
    ThermalBody hotBody{}, centerBody{}, coldBody{};
    hotBody.Temperature = 400.0f; centerBody.Temperature = 300.0f; coldBody.Temperature = 200.0f;
    hotBody.Material.SpecificHeatCapacity = 1.0f; centerBody.Material.SpecificHeatCapacity = 1.0f; coldBody.Material.SpecificHeatCapacity = 1.0f;
    ThermalWorld world{}; assert(world.RegisterBody(hotBody) == true); assert(world.RegisterBody(centerBody) == true); assert(world.RegisterBody(coldBody) == true);
    ThermalContact a{}; a.BodyA = &hotBody; a.BodyB = &centerBody; a.ThermalConductance = 10.0f; assert(world.RegisterContact(a) == true);
    ThermalContact b{}; b.BodyA = &centerBody; b.BodyB = &coldBody; b.ThermalConductance = 10.0f; assert(world.RegisterContact(b) == true);
    const float initialEnergy = CalculateThermalEnergy(hotBody) + CalculateThermalEnergy(centerBody) + CalculateThermalEnergy(coldBody);
    world.Step(1.0f);
    assert(world.GetLastSubstepCount() >= 40u && world.GetLastSubstepCount() <= 41u);
    assert(std::abs(CalculateThermalEnergy(hotBody) + CalculateThermalEnergy(centerBody) + CalculateThermalEnergy(coldBody) - initialEnergy) < 1.0e-3f);
    assert(std::abs(centerBody.Temperature - 300.0f) < 1.0e-3f);
}

void RunThermalConvectionTest()
{
    ThermalBody body{}; body.Temperature = 373.15f; body.Material.SpecificHeatCapacity = 100.0f;
    ThermalWorld world{}; assert(world.RegisterBody(body) == true);
    ThermalEnvironmentContact convection{}; convection.Body = &body; convection.AmbientTemperature = 293.15f;
    convection.HeatTransferCoefficient = 10.0f; convection.SurfaceArea = 2.0f;
    assert(world.RegisterEnvironmentContact(convection) == true);
    assert(std::abs(world.GetEnvironmentContacts().front().ThermalConductance - 20.0f) < 1.0e-6f);
    world.Step(1.0f); assert(body.Temperature < 373.15f && body.Temperature >= 293.15f);
    for (std::size_t stepIndex = 0u; stepIndex < 100u; ++stepIndex) { world.Step(1.0f); }
    assert(std::abs(body.Temperature - 293.15f) < 1.0e-3f);
}

void RunThermalRadiationTest()
{
    ThermalBody body{};
    body.Temperature = 800.0f;
    body.Mass = 1.0f;
    body.Material.SpecificHeatCapacity = 100.0f;

    ThermalWorld world{};
    assert(world.RegisterBody(body) == true);

    ThermalRadiationContact radiation{};
    radiation.Body = &body;
    radiation.EnvironmentTemperature = 300.0f;
    radiation.Emissivity = 1.0f;
    radiation.SurfaceArea = 1.0f;
    assert(world.RegisterRadiationContact(radiation) == true);
    assert(world.GetRadiationContactCount() == 1u);

    // 800K黒体から300K環境への正味放射は負(Bodyから放熱)になることを確認します。
    const float heatFlow = ThermalWorld::CalculateRadiationHeatFlow(800.0f, 300.0f, 1.0f, 1.0f);
    assert(heatFlow < 0.0f);
    assert(ThermalWorld::CalculateRadiationTangentConductance(800.0f, 1.0f, 1.0f) > 0.0f);

    const float initialTemperature = body.Temperature;
    world.Step(0.1f);
    assert(body.Temperature < initialTemperature);
    assert(body.Temperature >= 300.0f);

    // 非線形T^4熱流を反復し、Environment温度へ近づくことを確認します。
    for (std::size_t stepIndex = 0u; stepIndex < 2000u; ++stepIndex)
    {
        world.Step(0.1f);
    }
    assert(body.Temperature < 310.0f);
    assert(body.Temperature >= 300.0f);

    ThermalRadiationContact invalidRadiation{};
    invalidRadiation.Body = &body;
    invalidRadiation.Emissivity = 1.1f;
    assert(world.RegisterRadiationContact(invalidRadiation) == false);
}

void RunThermalEcsSynchronizationTest()
{
    Scene scene{};
    Entity hotEntity = scene.CreateEntity("ThermalHot"); Entity coldEntity = scene.CreateEntity("ThermalCold");
    hotEntity.AddComponent<ThermalBodyComponent>(); coldEntity.AddComponent<ThermalBodyComponent>();
    ThermalBodyComponent& hotComponent = hotEntity.GetComponent<ThermalBodyComponent>(); ThermalBodyComponent& coldComponent = coldEntity.GetComponent<ThermalBodyComponent>();
    hotComponent.Body.Temperature = 373.15f; coldComponent.Body.Temperature = 293.15f;
    ThermalContactComponent& link = hotEntity.AddComponent<ThermalContactComponent>(); link.TargetEntity = coldEntity.GetHandle(); link.ContactArea = 0.01f; link.ConductionDistance = 0.01f;
    ThermalConvectionComponent& convection = hotEntity.AddComponent<ThermalConvectionComponent>(); convection.AmbientTemperature = 293.15f; convection.HeatTransferCoefficient = 5.0f; convection.SurfaceArea = 2.0f;
    ThermalRadiationComponent& radiation = hotEntity.AddComponent<ThermalRadiationComponent>(); radiation.EnvironmentTemperature = 293.15f; radiation.Emissivity = 0.8f; radiation.SurfaceArea = 2.0f;
    ThermalSystem::SynchronizeWorld(scene);
    ThermalWorld& world = scene.GetPhysicsSimulationWorld().GetThermalWorld();
    assert(world.GetRegisteredBodyCount() == 2u); assert(world.GetContactCount() == 1u);
    assert(world.GetEnvironmentContactCount() == 1u); assert(world.GetRadiationContactCount() == 1u);
    world.Step(1.0f); assert(hotComponent.Body.Temperature < 373.15f); assert(coldComponent.Body.Temperature > 293.15f);
    scene.DestroyEntity(coldEntity); ThermalSystem::SynchronizeWorld(scene);
    assert(world.GetRegisteredBodyCount() == 1u); assert(world.GetContactCount() == 0u);
    assert(world.GetEnvironmentContactCount() == 1u); assert(world.GetRadiationContactCount() == 1u);
}

void RunRigidContactThermalCouplingTest()
{
    Scene scene{}; Entity hotEntity = scene.CreateEntity("RigidThermalHot"); Entity coldEntity = scene.CreateEntity("RigidThermalCold");
    hotEntity.AddComponent<ThermalBodyComponent>(); coldEntity.AddComponent<ThermalBodyComponent>(); hotEntity.AddComponent<ThermalRigidContactComponent>(); coldEntity.AddComponent<ThermalRigidContactComponent>();
    ThermalBodyComponent& hotComponent = hotEntity.GetComponent<ThermalBodyComponent>(); ThermalBodyComponent& coldComponent = coldEntity.GetComponent<ThermalBodyComponent>();
    hotComponent.Body.Temperature = 373.15f; coldComponent.Body.Temperature = 293.15f;
    ThermalRigidContactComponent& hotSettings = hotEntity.GetComponent<ThermalRigidContactComponent>(); ThermalRigidContactComponent& coldSettings = coldEntity.GetComponent<ThermalRigidContactComponent>();
    hotSettings.NominalContactAreaPerPoint = 0.002f; coldSettings.NominalContactAreaPerPoint = 0.001f; hotSettings.ConductionDistance = 0.02f; coldSettings.ConductionDistance = 0.04f; hotSettings.ConductivityScale = 1.0f; coldSettings.ConductivityScale = 0.25f;
    ThermalSystem::SynchronizeWorld(scene); ContactManifold manifold{}; manifold.A = hotEntity; manifold.B = coldEntity; manifold.AddPoint(ContactPoint{}); manifold.AddPoint(ContactPoint{});
    std::vector<ContactManifold> manifolds{ manifold }; ThermalSystem::AppendRigidBodyContacts(scene, manifolds);
    ThermalWorld& world = scene.GetPhysicsSimulationWorld().GetThermalWorld(); assert(world.GetContactCount() == 1u);
    const ThermalContact& generatedContact = world.GetContacts().front(); assert(std::abs(generatedContact.ContactArea - 0.002f) < 1.0e-6f); assert(generatedContact.ThermalConductance > 0.0f);
    world.Step(1.0f); assert(hotComponent.Body.Temperature < 373.15f); assert(coldComponent.Body.Temperature > 293.15f);
}

void RunPhysicsSimulationThermalContactTest()
{
    Scene scene{}; Entity hotEntity = scene.CreateEntity("PhysicsThermalHot"); Entity coldEntity = scene.CreateEntity("PhysicsThermalCold");
    hotEntity.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.0f }; coldEntity.GetComponent<TransformComponent>().Position = { 0.9f, 0.0f, 0.0f };
    RigidBodyComponent rigidBody{}; rigidBody.UseGravity = false; rigidBody.AllowSleep = false; hotEntity.AddComponent<RigidBodyComponent>(rigidBody); coldEntity.AddComponent<RigidBodyComponent>(rigidBody);
    ColliderComponent collider{}; collider.Type = ColliderType::Sphere; collider.Radius = 0.5f; hotEntity.AddComponent<ColliderComponent>(collider); coldEntity.AddComponent<ColliderComponent>(collider);
    hotEntity.AddComponent<ThermalBodyComponent>(); coldEntity.AddComponent<ThermalBodyComponent>(); hotEntity.AddComponent<ThermalRigidContactComponent>(); coldEntity.AddComponent<ThermalRigidContactComponent>();
    ThermalBodyComponent& hotThermal = hotEntity.GetComponent<ThermalBodyComponent>(); ThermalBodyComponent& coldThermal = coldEntity.GetComponent<ThermalBodyComponent>(); hotThermal.Body.Temperature = 373.15f; coldThermal.Body.Temperature = 293.15f;
    hotEntity.GetComponent<ThermalRigidContactComponent>().NominalContactAreaPerPoint = 0.01f; coldEntity.GetComponent<ThermalRigidContactComponent>().NominalContactAreaPerPoint = 0.01f;
    hotEntity.GetComponent<ThermalRigidContactComponent>().ConductionDistance = 0.01f; coldEntity.GetComponent<ThermalRigidContactComponent>().ConductionDistance = 0.01f;
    PhysicsSimulationWorld& simulationWorld = scene.GetPhysicsSimulationWorld(); simulationWorld.StepSimulation(scene, 1.0f / 60.0f);
    assert(simulationWorld.GetRigidBodyWorld().GetContactManifolds().empty() == false); assert(simulationWorld.GetThermalWorld().GetContactCount() == 1u);
    assert(hotThermal.Body.Temperature < 373.15f); assert(coldThermal.Body.Temperature > 293.15f);
}
}

void RunThermalWorldSelfTests()
{
    RunThermalConductionTest();
    RunThermalNetworkSubstepTest();
    RunThermalConvectionTest();
    RunThermalRadiationTest();
    RunThermalEcsSynchronizationTest();
    RunRigidContactThermalCouplingTest();
    RunPhysicsSimulationThermalContactTest();
}

}
