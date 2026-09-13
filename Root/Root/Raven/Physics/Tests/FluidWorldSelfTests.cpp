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

        // Couplingより先にSimulationが実行されたことを確認できるよう、
        // Static Sphere内部へParticleを配置します。
        if (m_Particles.empty() == false)
        {
            m_Particles[0].Position = { 0.75f, 0.0f, 0.0f };
            m_Particles[0].Velocity = { -1.0f, 0.0f, 0.0f };
        }
    }

    FluidCouplingBinding* GetFluidCouplingBinding() override
    {
        return &m_CouplingBinding;
    }

    void SynchronizeFluidOutput() override
    {
        ++SynchronizationCount;
    }

    uint32_t StepCount = 0u;
    uint32_t SynchronizationCount = 0u;
    float LastFixedDeltaTime = 0.0f;

private:
    std::vector<FluidParticle>& m_Particles;
    FluidCouplingBinding m_CouplingBinding{};
};

bool IsNearlyEqual(float left, float right)
{
    return std::abs(left - right) <= 1.0e-5f;
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

    // 同じParticle配列へCouplingを二重登録すると位置補正やImpulseが重複するため拒否します。
    assert(fluidWorld.RegisterSimulationParticipant(duplicateParticleParticipant) == false);
    assert(fluidWorld.GetRegisteredSimulationParticipantCount() == 1u);
    assert(fluidWorld.GetCouplingBindingCount() == 1u);

    // RigidBodyを持たないSphere ColliderはStatic Fluid Coupling対象です。
    Entity staticSphereEntity = scene.CreateEntity("FluidWorld Self Test Static Sphere");
    ColliderComponent staticSphereCollider{};
    staticSphereCollider.Type = ColliderType::Sphere;
    staticSphereCollider.Radius = 1.0f;
    staticSphereCollider.Restitution = 0.0f;
    staticSphereEntity.AddComponent<ColliderComponent>(staticSphereCollider);

    constexpr float fixedDeltaTime = 1.0f / 60.0f;
    fluidWorld.StepSimulation(
        scene,
        simulationWorld.GetRigidBodyWorld(),
        fixedDeltaTime);

    assert(firstParticipant.StepCount == 1u);
    assert(firstParticipant.SynchronizationCount == 0u);
    assert(IsNearlyEqual(firstParticipant.LastFixedDeltaTime, fixedDeltaTime));

    // Simulationでx=0.75へ移したParticleは、半径1.0のSphereとParticle半径0.5の合計境界
    // x=1.5まで同じFixed Step内のStatic Couplingで押し出されます。
    assert(IsNearlyEqual(particles[0].Position.x, 1.5f));
    assert(IsNearlyEqual(particles[0].Position.y, 0.0f));
    assert(IsNearlyEqual(particles[0].Position.z, 0.0f));
    assert(IsNearlyEqual(particles[0].Velocity.x, 0.0f));

    const FluidStaticColliderCouplingStatistics& staticStatistics =
        fluidWorld.GetLastStaticColliderCouplingStatistics();
    assert(staticStatistics.SupportedColliderCount == 1u);
    assert(staticStatistics.CandidatePairCount == 1u);
    assert(staticStatistics.ResolvedContactCount == 1u);

    // catch-up中のSimulation/Couplingと外部出力同期は分離されます。
    fluidWorld.SynchronizeOutputs();
    assert(firstParticipant.SynchronizationCount == 1u);

    assert(fluidWorld.UnregisterSimulationParticipant(firstParticipant) == true);
    assert(fluidWorld.UnregisterSimulationParticipant(firstParticipant) == false);
    assert(fluidWorld.ContainsSimulationParticipant(firstParticipant) == false);
    assert(fluidWorld.ContainsCouplingBinding(particles) == false);
    assert(fluidWorld.GetRegisteredSimulationParticipantCount() == 0u);
    assert(fluidWorld.GetCouplingBindingCount() == 0u);

    // Unregister後はParticipantもCouplingも実行されません。
    particles[0].Position = { 0.75f, 0.0f, 0.0f };
    fluidWorld.StepSimulation(
        scene,
        simulationWorld.GetRigidBodyWorld(),
        fixedDeltaTime);
    assert(firstParticipant.StepCount == 1u);
    assert(IsNearlyEqual(particles[0].Position.x, 0.75f));

    // nullptr Particle BindingはRegistryへ受け入れません。
    FluidCouplingBinding invalidBinding{};
    assert(fluidWorld.RegisterCouplingBinding(invalidBinding) == false);

    fluidWorld.Clear();
    assert(fluidWorld.GetRegisteredSimulationParticipantCount() == 0u);
    assert(fluidWorld.GetCouplingBindingCount() == 0u);
}

} // namespace Raven::ph::tests
