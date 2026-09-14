#include "Raven/Physics/Tests/FluidWorldSelfTests.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph::tests
{
namespace
{
class TestFluidSimulationParticipant final : public FluidSimulationParticipant
{
public:
    explicit TestFluidSimulationParticipant(std::vector<FluidParticle>& particles)
        : m_Particles(particles)
    {
        m_CouplingBinding.Particles = &m_Particles;
        m_CouplingBinding.StaticColliderSettings.ParticleRadius = 0.5f;
        m_CouplingBinding.StaticColliderSettings.Restitution = 0.0f;
        m_CouplingBinding.RigidBodyCouplingEnabled = false;
    }

    void SimulateFluid(float fixedDeltaTime) override
    {
        ++StepCount;
        LastFixedDeltaTime = fixedDeltaTime;
        if (m_Particles.empty() == false)
        {
            m_Particles[0].Position = { 0.75f, 0.0f, 0.0f };
            m_Particles[0].Velocity = { -1.0f, 0.0f, 0.0f };
        }
    }

    FluidCouplingBinding* GetFluidCouplingBinding() override { return &m_CouplingBinding; }
    void SynchronizeFluidOutput() override { ++SynchronizationCount; }

    uint32_t StepCount = 0u;
    uint32_t SynchronizationCount = 0u;
    float LastFixedDeltaTime = 0.0f;

private:
    std::vector<FluidParticle>& m_Particles;
    FluidCouplingBinding m_CouplingBinding{};
};

class TestFluidRigidBodyParticipant final : public FluidSimulationParticipant
{
public:
    explicit TestFluidRigidBodyParticipant(std::vector<FluidParticle>& particles)
        : m_Particles(particles)
    {
        m_CouplingBinding.Particles = &m_Particles;
        m_CouplingBinding.StaticColliderCouplingEnabled = false;
        m_CouplingBinding.RigidBodyCouplingEnabled = true;
        m_CouplingBinding.RigidBodySettings.ParticleRadius = 0.1f;
        m_CouplingBinding.RigidBodySettings.Restitution = 0.0f;
        m_CouplingBinding.RigidBodySettings.DragCoefficient = 0.5f;
        m_CouplingBinding.RigidBodySettings.PressureReactionCoefficient = 1.0f;
        m_CouplingBinding.RigidBodySettings.BuoyancyCoefficient = 1.0f;
    }

    void SimulateFluid(float fixedDeltaTime) override
    {
        ++StepCount;
        LastFixedDeltaTime = fixedDeltaTime;
        if (m_Particles.empty() == false)
        {
            m_Particles[0].Position = { 0.0f, 0.5f, 0.0f };
            m_Particles[0].Velocity = { 1.0f, -1.0f, 0.0f };
            m_Particles[0].Mass = 2.0f;
            m_Particles[0].Pressure = 10.0f;
        }
    }

    FluidCouplingBinding* GetFluidCouplingBinding() override { return &m_CouplingBinding; }

    uint32_t StepCount = 0u;
    float LastFixedDeltaTime = 0.0f;

private:
    std::vector<FluidParticle>& m_Particles;
    FluidCouplingBinding m_CouplingBinding{};
};

bool IsNearlyEqual(float left, float right)
{
    return std::abs(left - right) <= 1.0e-5f;
}

void RunFluidWorldRigidBodyCouplingTest()
{
    Scene scene;
    PhysicsSimulationWorld& simulationWorld = scene.GetPhysicsSimulationWorld();
    FluidWorld& fluidWorld = simulationWorld.GetFluidWorld();
    PhysicsWorld& rigidBodyWorld = simulationWorld.GetRigidBodyWorld();
    rigidBodyWorld.SetGravity({ 0.0f, -10.0f, 0.0f });

    Entity bodyEntity = scene.CreateEntity("FluidWorld RigidBody Coupling Sphere");
    RigidBodyComponent rigidBody{};
    rigidBody.SetBodyType(BodyType::Dynamic);
    rigidBody.SetMass(2.0f);
    rigidBody.UseGravity = false;
    rigidBody.AllowSleep = false;
    bodyEntity.AddComponent<RigidBodyComponent>(rigidBody);

    ColliderComponent collider{};
    collider.Type = ColliderType::Sphere;
    collider.Radius = 0.5f;
    collider.Restitution = 0.0f;
    bodyEntity.AddComponent<ColliderComponent>(collider);

    std::vector<FluidParticle> particles(1u);
    TestFluidRigidBodyParticipant participant(particles);
    assert(fluidWorld.RegisterSimulationParticipant(participant) == true);

    constexpr float fixedDeltaTime = 0.1f;
    fluidWorld.StepSimulation(scene, rigidBodyWorld, fixedDeltaTime);

    assert(participant.StepCount == 1u);
    assert(IsNearlyEqual(participant.LastFixedDeltaTime, fixedDeltaTime));

    const RigidBodyComponent& resolvedBody = bodyEntity.GetComponent<RigidBodyComponent>();
    const FluidRigidBodyCouplingStatistics& statistics = fluidWorld.GetLastRigidBodyCouplingStatistics();
    assert(statistics.DynamicBodyCount == 1u);
    assert(statistics.CandidatePairCount == 1u);
    assert(statistics.ResolvedContactCount == 1u);
    assert(statistics.AppliedImpulseCount == 1u);
    assert(statistics.AppliedDragImpulseCount == 1u);
    assert(statistics.AppliedPressureImpulseCount == 1u);
    assert(statistics.AppliedBuoyancyImpulseCount == 1u);
    assert(statistics.TotalNormalImpulse > 0.0f);
    assert(statistics.TotalDragImpulse > 0.0f);
    assert(statistics.TotalPressureImpulse > 0.0f);
    assert(statistics.TotalBuoyancyImpulse > 0.0f);
    assert(statistics.TotalDisplacedFluidMass > 0.0f);
    assert(resolvedBody.LinearVelocity.x > 0.0f);
    assert(std::abs(resolvedBody.LinearVelocity.y) > 1.0e-5f);
    assert(particles[0].Velocity.x < 1.0f);

    assert(fluidWorld.UnregisterSimulationParticipant(participant) == true);
    assert(fluidWorld.GetRegisteredSimulationParticipantCount() == 0u);
    assert(fluidWorld.GetCouplingBindingCount() == 0u);
}

void RunFluidWorldMultipleBindingStatisticsTest()
{
    Scene scene;
    PhysicsSimulationWorld& simulationWorld = scene.GetPhysicsSimulationWorld();
    FluidWorld& fluidWorld = simulationWorld.GetFluidWorld();

    Entity staticSphereEntity = scene.CreateEntity("FluidWorld Aggregate Statistics Sphere");
    ColliderComponent collider{};
    collider.Type = ColliderType::Sphere;
    collider.Radius = 1.0f;
    collider.Restitution = 0.0f;
    staticSphereEntity.AddComponent<ColliderComponent>(collider);

    std::vector<FluidParticle> firstParticles(1u);
    std::vector<FluidParticle> secondParticles(1u);
    TestFluidSimulationParticipant firstParticipant(firstParticles);
    TestFluidSimulationParticipant secondParticipant(secondParticles);

    assert(fluidWorld.RegisterSimulationParticipant(firstParticipant) == true);
    assert(fluidWorld.RegisterSimulationParticipant(secondParticipant) == true);

    constexpr float fixedDeltaTime = 1.0f / 60.0f;
    fluidWorld.StepSimulation(scene, simulationWorld.GetRigidBodyWorld(), fixedDeltaTime);

    // Coupling Solver単体ではResolveSceneごとにStatisticsが初期化されます。
    // FluidWorld公開値は2つのBindingを同じfixed-step内で合算し、最後のBindingだけへ
    // 上書きされないことを固定します。
    const FluidStaticColliderCouplingStatistics& statistics =
        fluidWorld.GetLastStaticColliderCouplingStatistics();
    assert(statistics.SupportedColliderCount == 2u);
    assert(statistics.CandidatePairCount == 2u);
    assert(statistics.ResolvedContactCount == 2u);

    // 次fixed-stepでは前step値へ累積せず、そのstepの2 Binding分だけを再集計します。
    fluidWorld.StepSimulation(scene, simulationWorld.GetRigidBodyWorld(), fixedDeltaTime);
    const FluidStaticColliderCouplingStatistics& nextStatistics =
        fluidWorld.GetLastStaticColliderCouplingStatistics();
    assert(nextStatistics.SupportedColliderCount == 2u);
    assert(nextStatistics.CandidatePairCount == 2u);
    assert(nextStatistics.ResolvedContactCount == 2u);

    assert(fluidWorld.UnregisterSimulationParticipant(firstParticipant) == true);
    assert(fluidWorld.UnregisterSimulationParticipant(secondParticipant) == true);
    assert(fluidWorld.GetLastStaticColliderCouplingStatistics().SupportedColliderCount == 0u);
}
}

void RunFluidWorldSelfTests()
{
    Scene scene;
    PhysicsSimulationWorld& simulationWorld = scene.GetPhysicsSimulationWorld();
    FluidWorld& fluidWorld = simulationWorld.GetFluidWorld();
    const PhysicsSimulationWorld& constSimulationWorld = simulationWorld;
    const FluidWorld& constFluidWorld = constSimulationWorld.GetFluidWorld();

    assert(&fluidWorld == &constFluidWorld);
    assert(fluidWorld.GetRegisteredSimulationParticipantCount() == 0u);
    assert(fluidWorld.GetCouplingBindingCount() == 0u);

    std::vector<FluidParticle> particles(1u);
    particles[0].Position = { 5.0f, 0.0f, 0.0f };
    TestFluidSimulationParticipant firstParticipant(particles);
    TestFluidSimulationParticipant duplicateParticleParticipant(particles);

    assert(fluidWorld.RegisterSimulationParticipant(firstParticipant) == true);
    assert(fluidWorld.RegisterSimulationParticipant(firstParticipant) == false);
    assert(fluidWorld.ContainsSimulationParticipant(firstParticipant) == true);
    assert(fluidWorld.GetRegisteredSimulationParticipantCount() == 1u);
    assert(fluidWorld.GetCouplingBindingCount() == 1u);
    assert(fluidWorld.ContainsCouplingBinding(particles) == true);
    assert(fluidWorld.RegisterSimulationParticipant(duplicateParticleParticipant) == false);
    assert(fluidWorld.GetRegisteredSimulationParticipantCount() == 1u);
    assert(fluidWorld.GetCouplingBindingCount() == 1u);

    Entity staticSphereEntity = scene.CreateEntity("FluidWorld Self Test Static Sphere");
    ColliderComponent staticSphereCollider{};
    staticSphereCollider.Type = ColliderType::Sphere;
    staticSphereCollider.Radius = 1.0f;
    staticSphereCollider.Restitution = 0.0f;
    staticSphereEntity.AddComponent<ColliderComponent>(staticSphereCollider);

    constexpr float fixedDeltaTime = 1.0f / 60.0f;
    fluidWorld.StepSimulation(scene, simulationWorld.GetRigidBodyWorld(), fixedDeltaTime);
    assert(firstParticipant.StepCount == 1u);
    assert(firstParticipant.SynchronizationCount == 0u);
    assert(IsNearlyEqual(firstParticipant.LastFixedDeltaTime, fixedDeltaTime));
    assert(IsNearlyEqual(particles[0].Position.x, 1.5f));
    assert(IsNearlyEqual(particles[0].Position.y, 0.0f));
    assert(IsNearlyEqual(particles[0].Position.z, 0.0f));
    assert(IsNearlyEqual(particles[0].Velocity.x, 0.0f));

    const FluidStaticColliderCouplingStatistics& staticStatistics = fluidWorld.GetLastStaticColliderCouplingStatistics();
    assert(staticStatistics.SupportedColliderCount == 1u);
    assert(staticStatistics.CandidatePairCount == 1u);
    assert(staticStatistics.ResolvedContactCount == 1u);

    fluidWorld.SynchronizeOutputs();
    assert(firstParticipant.SynchronizationCount == 1u);
    assert(fluidWorld.UnregisterSimulationParticipant(firstParticipant) == true);
    assert(fluidWorld.UnregisterSimulationParticipant(firstParticipant) == false);
    assert(fluidWorld.ContainsSimulationParticipant(firstParticipant) == false);
    assert(fluidWorld.ContainsCouplingBinding(particles) == false);
    assert(fluidWorld.GetRegisteredSimulationParticipantCount() == 0u);
    assert(fluidWorld.GetCouplingBindingCount() == 0u);

    particles[0].Position = { 0.75f, 0.0f, 0.0f };
    fluidWorld.StepSimulation(scene, simulationWorld.GetRigidBodyWorld(), fixedDeltaTime);
    assert(firstParticipant.StepCount == 1u);
    assert(IsNearlyEqual(particles[0].Position.x, 0.75f));

    FluidCouplingBinding invalidBinding{};
    assert(fluidWorld.RegisterCouplingBinding(invalidBinding) == false);
    fluidWorld.Clear();
    assert(fluidWorld.GetRegisteredSimulationParticipantCount() == 0u);
    assert(fluidWorld.GetCouplingBindingCount() == 0u);

    RunFluidWorldRigidBodyCouplingTest();
    RunFluidWorldMultipleBindingStatisticsTest();
}

} // namespace Raven::ph::tests
