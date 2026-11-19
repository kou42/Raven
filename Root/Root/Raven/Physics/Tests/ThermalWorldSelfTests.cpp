#include "Raven/Physics/Contact.h"
#include "Raven/Physics/Coupling/GridThermalCoupling.h"
#include "Raven/Physics/Electromagnetism/GridElectromagnetism.h"
#include "Raven/Physics/Thermal/ThermalComponents.h"
#include "Raven/Physics/Thermal/ThermalSystem.h"
#include "Raven/Physics/Thermal/ThermalWorld.h"
#include "Raven/Scene/Scene.h"

#include <cstdio>
#include <utility>
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

    ThermalBody coldBody = hotBody;
    coldBody.Temperature = 293.15f;

    ThermalWorld world{};
    assert(world.RegisterBody(hotBody) == true);
    assert(world.RegisterBody(coldBody) == true);

    ThermalContact contact{};
    contact.BodyA = &hotBody;
    contact.BodyB = &coldBody;
    contact.ContactArea = 0.01f;
    contact.ConductionDistance = 0.01f;
    assert(world.RegisterContact(contact) == true);

    const float initialEnergy = CalculateThermalEnergy(hotBody) + CalculateThermalEnergy(coldBody);

    world.Step(1.0f);

    assert(hotBody.Temperature < 373.15f);
    assert(coldBody.Temperature > 293.15f);
    assert(std::abs(CalculateThermalEnergy(hotBody) + CalculateThermalEnergy(coldBody) - initialEnergy) < 1.0f);

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

    assert(world.GetLastSubstepCount() >= 40u && world.GetLastSubstepCount() <= 41u);
    assert(std::abs(
        CalculateThermalEnergy(hotBody)
        + CalculateThermalEnergy(centerBody)
        + CalculateThermalEnergy(coldBody)
        - initialEnergy) < 1.0e-3f);
    assert(std::abs(centerBody.Temperature - 300.0f) < 1.0e-3f);
}

void RunThermalConvectionTest()
{
    ThermalBody body{};
    body.Temperature = 373.15f;
    body.Material.SpecificHeatCapacity = 100.0f;

    ThermalWorld world{};
    assert(world.RegisterBody(body) == true);

    ThermalEnvironmentContact convection{};
    convection.Body = &body;
    convection.AmbientTemperature = 293.15f;
    convection.HeatTransferCoefficient = 10.0f;
    convection.SurfaceArea = 2.0f;
    assert(world.RegisterEnvironmentContact(convection) == true);

    assert(std::abs(world.GetEnvironmentContacts().front().ThermalConductance - 20.0f) < 1.0e-6f);

    world.Step(1.0f);
    assert(body.Temperature < 373.15f && body.Temperature >= 293.15f);

    for (std::size_t stepIndex = 0u; stepIndex < 100u; ++stepIndex)
    {
        world.Step(1.0f);
    }

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

    const float heatFlow = ThermalWorld::CalculateRadiationHeatFlow(800.0f, 300.0f, 1.0f, 1.0f);
    assert(heatFlow < 0.0f);
    assert(ThermalWorld::CalculateRadiationTangentConductance(800.0f, 1.0f, 1.0f) > 0.0f);

    const float initialTemperature = body.Temperature;
    world.Step(0.1f);

    assert(body.Temperature < initialTemperature);
    assert(body.Temperature >= 300.0f);

    for (std::size_t stepIndex = 0u; stepIndex < 10000u; ++stepIndex)
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

void RunThermalPhaseChangeTest()
{
    ThermalBody body{};
    body.Temperature = 263.15f;
    body.Mass = 1.0f;
    body.Material.SpecificHeatCapacity = 100.0f;
    body.PhaseChangeTemperature = 273.15f;
    body.LatentHeatOfFusion = 1000.0f;
    body.LiquidSpecificHeatCapacity = 200.0f;

    // 1000Jで固相を10K加熱して融点へ到達します。
    body.ApplyHeat(1000.0f);
    assert(std::abs(body.Temperature - 273.15f) < 1.0e-4f);
    assert(std::abs(body.MeltFraction) < 1.0e-6f);

    // 次の500Jは温度を変えず潜熱へ入り、半分だけ融解します。
    body.ApplyHeat(500.0f);
    assert(std::abs(body.Temperature - 273.15f) < 1.0e-4f);
    assert(std::abs(body.MeltFraction - 0.5f) < 1.0e-6f);

    body.ApplyHeat(500.0f);
    assert(std::abs(body.MeltFraction - 1.0f) < 1.0e-6f);
    assert(std::abs(body.Temperature - 273.15f) < 1.0e-4f);

    // 完全融解後は液相比熱で温度が上昇します。
    body.ApplyHeat(200.0f);
    assert(std::abs(body.Temperature - 274.15f) < 1.0e-4f);

    // 冷却時は逆順に、液相顕熱→凝固潜熱→固相顕熱へ戻ります。
    body.ApplyHeat(-200.0f);
    assert(std::abs(body.Temperature - 273.15f) < 1.0e-4f);

    body.ApplyHeat(-500.0f);
    assert(std::abs(body.MeltFraction - 0.5f) < 1.0e-6f);

    body.ApplyHeat(-500.0f);
    assert(std::abs(body.MeltFraction) < 1.0e-6f);

    body.ApplyHeat(-1000.0f);
    assert(std::abs(body.Temperature - 263.15f) < 1.0e-4f);

    // Solver経由でも熱量がApplyHeatへ流れ、潜熱が温度上昇より先に消費されることを確認します。
    body.Temperature = 273.15f;
    body.MeltFraction = 0.0f;

    ThermalWorld world{};
    assert(world.RegisterBody(body) == true);

    ThermalEnvironmentContact environment{};
    environment.Body = &body;
    environment.AmbientTemperature = 373.15f;
    environment.ThermalConductance = 10.0f;
    assert(world.RegisterEnvironmentContact(environment) == true);

    world.Step(0.5f);

    assert(std::abs(body.Temperature - 273.15f) < 1.0e-4f);
    assert(body.MeltFraction > 0.0f);
    assert(body.MeltFraction < 1.0f);
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

    ThermalContactComponent& link = hotEntity.AddComponent<ThermalContactComponent>();
    link.TargetEntity = coldEntity.GetHandle();
    link.ContactArea = 0.01f;
    link.ConductionDistance = 0.01f;

    ThermalConvectionComponent& convection = hotEntity.AddComponent<ThermalConvectionComponent>();
    convection.AmbientTemperature = 293.15f;
    convection.HeatTransferCoefficient = 5.0f;
    convection.SurfaceArea = 2.0f;

    ThermalRadiationComponent& radiation = hotEntity.AddComponent<ThermalRadiationComponent>();
    radiation.EnvironmentTemperature = 293.15f;
    radiation.Emissivity = 0.8f;
    radiation.SurfaceArea = 2.0f;

    ThermalSystem::SynchronizeWorld(scene);

    ThermalWorld& world = scene.GetPhysicsSimulationWorld().GetThermalWorld();
    assert(world.GetRegisteredBodyCount() == 2u);
    assert(world.GetContactCount() == 1u);
    assert(world.GetEnvironmentContactCount() == 1u);
    assert(world.GetRadiationContactCount() == 1u);

    world.Step(1.0f);

    assert(hotComponent.Body.Temperature < 373.15f);
    assert(coldComponent.Body.Temperature > 293.15f);

    scene.DestroyEntity(coldEntity);
    ThermalSystem::SynchronizeWorld(scene);

    assert(world.GetRegisteredBodyCount() == 1u);
    assert(world.GetContactCount() == 0u);
    assert(world.GetEnvironmentContactCount() == 1u);
    assert(world.GetRadiationContactCount() == 1u);
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

    ThermalRigidContactComponent& hotSettings = hotEntity.GetComponent<ThermalRigidContactComponent>();
    ThermalRigidContactComponent& coldSettings = coldEntity.GetComponent<ThermalRigidContactComponent>();

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
    assert(generatedContact.ThermalConductance > 0.0f);

    world.Step(1.0f);

    assert(hotComponent.Body.Temperature < 373.15f);
    assert(coldComponent.Body.Temperature > 293.15f);
}

void RunPhysicsSimulationThermalContactTest()
{
    Scene scene{};

    Entity hotEntity = scene.CreateEntity("PhysicsThermalHot");
    Entity coldEntity = scene.CreateEntity("PhysicsThermalCold");

    hotEntity.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.0f };
    coldEntity.GetComponent<TransformComponent>().Position = { 0.9f, 0.0f, 0.0f };

    RigidBodyComponent rigidBody{};
    rigidBody.UseGravity = false;
    rigidBody.AllowSleep = false;
    hotEntity.AddComponent<RigidBodyComponent>(rigidBody);
    coldEntity.AddComponent<RigidBodyComponent>(rigidBody);

    ColliderComponent collider{};
    collider.Type = ColliderType::Sphere;
    collider.Radius = 0.5f;
    hotEntity.AddComponent<ColliderComponent>(collider);
    coldEntity.AddComponent<ColliderComponent>(collider);

    hotEntity.AddComponent<ThermalBodyComponent>();
    coldEntity.AddComponent<ThermalBodyComponent>();
    hotEntity.AddComponent<ThermalRigidContactComponent>();
    coldEntity.AddComponent<ThermalRigidContactComponent>();

    ThermalBodyComponent& hotThermal = hotEntity.GetComponent<ThermalBodyComponent>();
    ThermalBodyComponent& coldThermal = coldEntity.GetComponent<ThermalBodyComponent>();
    hotThermal.Body.Temperature = 373.15f;
    coldThermal.Body.Temperature = 293.15f;

    hotEntity.GetComponent<ThermalRigidContactComponent>().NominalContactAreaPerPoint = 0.01f;
    coldEntity.GetComponent<ThermalRigidContactComponent>().NominalContactAreaPerPoint = 0.01f;
    hotEntity.GetComponent<ThermalRigidContactComponent>().ConductionDistance = 0.01f;
    coldEntity.GetComponent<ThermalRigidContactComponent>().ConductionDistance = 0.01f;

    PhysicsSimulationWorld& simulationWorld = scene.GetPhysicsSimulationWorld();
    simulationWorld.StepSimulation(scene, 1.0f / 60.0f);

    assert(simulationWorld.GetRigidBodyWorld().GetContactManifolds().empty() == false);
    assert(simulationWorld.GetThermalWorld().GetContactCount() == 1u);
    assert(hotThermal.Body.Temperature < 373.15f);
    assert(coldThermal.Body.Temperature > 293.15f);
}
}


bool RunGridThermalSelfTests()
{
    const DenseSDFGrid grid{ {}, { 1.0f, 1.0f, 1.0f }, { 3, 1, 1 } };
    const std::array material{ ElectromagneticMaterial{ 2.0, 1.0, 1.0 } };
    const auto materialGrid = ElectromagneticMaterialGrid::Build(grid, material, 0, {});
    const std::array<std::optional<double>, 3> electrode{ 4.0, std::nullopt, 0.0 };
    std::shared_ptr<const ElectromagneticPotentialResult> electrical;
    if (materialGrid.has_value() == false ||
        SolveElectromagneticPotential(*materialGrid, ElectromagneticPotentialMode::SteadyCurrent,
            electrode, {}, 100, 1.0e-12, electrical) != ElectromagneticSolveStatus::Converged)
    {
        return false;
    }

    const std::array initial{ 300.0, 300.0, 300.0 };
    auto temperature = GridTemperatureField::Create(grid, 2.0, 0.0, initial);
    if (temperature.has_value() == false)
    {
        return false;
    }
    auto thermal = std::make_shared<GridThermalCoupling>(std::move(*temperature));
    if (thermal->SetJouleHeating(electrical) == false)
    {
        return false;
    }
    const std::array particle{ GridHeatParticle{ grid.GetCellCenter(1, 0, 0), 4.0 } };
    thermal->SetParticles(particle);
    Scene scene;
    auto& world = scene.GetPhysicsSimulationWorld();
    world.SetGridThermalCoupling(thermal);
    const auto probe = grid.GetCellCenter(1, 0, 0);
    auto probeEntity = scene.CreateEntity("Grid temperature probe");
    probeEntity.GetComponent<TransformComponent>().Position = probe;
    auto& probeBody = probeEntity.AddComponent<ThermalBodyComponent>().Body;
    probeBody.Temperature = 300.0f;
    probeBody.Material.SpecificHeatCapacity = 1.0f;
    auto& convection = probeEntity.AddComponent<ThermalConvectionComponent>();
    convection.HeatTransferCoefficient = 1.0f;
    convection.SurfaceArea = 1.0f;
    world.StepSimulation(scene, 0.25f);
    const double energyError = std::abs(thermal->GetField().GetTotalEnergy() - 1804.0);
    const auto& registry = world.GetThermalWorld().GetTemperatureFieldRegistry();
    if (thermal->GetLastStatus() != GridThermalStepStatus::Advanced || energyError > 1.0e-10 || probeBody.Temperature <= 300.0f ||
        std::abs(registry.Evaluate(probe, 0.0f) - 301.5f) > 1.0e-6f ||
        registry.Evaluate({ -1.0f, 0.0f, 0.0f }, 275.0f) != 275.0f)
    {
        return false;
    }

    class TestFluid final : public FluidSimulationParticipant, public VectorField
    {
    public:
        void SimulateFluid(float) override { m_Speed = 1.0f; }
        math::Vec3 Evaluate(const math::Vec3&) const override { return { m_Speed, 0.0f, 0.0f }; }
    private:
        float m_Speed = 0.0f;
    };
    const std::array hotCell{ 100.0, 0.0, 0.0 };
    auto transport = GridTemperatureField::Create(grid, 1.0, 0.0, hotCell);
    if (transport.has_value() == false)
    {
        return false;
    }
    const auto fluid = std::make_shared<TestFluid>();
    auto advected = std::make_shared<GridThermalCoupling>(std::move(*transport));
    advected->SetFluidVelocity(fluid);
    if (world.GetFluidWorld().RegisterSimulationParticipant(*fluid) == false)
    {
        return false;
    }
    world.SetGridThermalCoupling(advected);
    world.StepSimulation(scene, 0.25f);
    const auto translated = advected->GetField().GetTemperatures();
    if (registry.ContainsField(thermal->GetField()) || advected->GetLastStatus() != GridThermalStepStatus::Advanced ||
        translated[0] != 75.0 || translated[1] != 25.0 || translated[2] != 0.0 ||
        advected->GetField().GetTotalEnergy() != 100.0)
    {
        return false;
    }
    world.GetFluidWorld().UnregisterSimulationParticipant(*fluid);
    world.SetGridThermalCoupling(nullptr);

    const DenseSDFGrid reactionGrid{ {}, { 1.0f, 1.0f, 1.0f }, { 2, 1, 1 } };
    const std::array ignitionTemperature{ 500.0, 299.0 };
    auto diffused = GridTemperatureField::Create(reactionGrid, 1.0, 1.0, ignitionTemperature);
    if (diffused.has_value() == false)
    {
        return false;
    }
    GridThermalCoupling reaction(std::move(*diffused));
    const std::array fuel{ 1.0, 1.0 };
    if (reaction.SetCombustion({ 300.0, 1.0, 100.0 }, fuel) == false)
    {
        return false;
    }
    reaction.Step(2.0f);
    if (reaction.GetLastStatus() != GridThermalStepStatus::UnstableStep ||
        reaction.GetFuelDensity()[0] != 1.0 || reaction.GetField().GetTemperatures()[0] != 500.0)
    {
        return false;
    }
    reaction.Step(0.25f);
    if (reaction.GetLastStatus() != GridThermalStepStatus::Advanced || reaction.GetFuelDensity()[1] != 1.0 ||
        reaction.GetField().GetTemperatures()[1] != 349.25 || reaction.GetField().GetTotalEnergy() != 824.0)
    {
        return false;
    }
    reaction.Step(0.25f);
    if (reaction.GetLastStatus() != GridThermalStepStatus::Advanced || reaction.GetFuelDensity()[1] != 0.75 ||
        reaction.GetField().GetTotalEnergy() != 874.0)
    {
        return false;
    }
    std::printf("Phase 09: heat balance error=%.9g J, advected heat=25 J, ignition propagated after 1 step\n", energyError);
    return true;
}

void RunThermalWorldSelfTests()
{
    RunThermalConductionTest();
    RunThermalNetworkSubstepTest();
    RunThermalConvectionTest();
    RunThermalRadiationTest();
    RunThermalPhaseChangeTest();
    RunThermalEcsSynchronizationTest();
    RunRigidContactThermalCouplingTest();
    RunPhysicsSimulationThermalContactTest();
}

}
