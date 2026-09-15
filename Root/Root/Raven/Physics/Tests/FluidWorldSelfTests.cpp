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

    void SetStaticColliderCouplingEnabled(bool enabled)
    {
        m_CouplingBinding.StaticColliderCouplingEnabled = enabled;
    }

    void SetStaticColliderParticleRadius(float particleRadius)
    {
        m_CouplingBinding.StaticColliderSettings.ParticleRadius = particleRadius;
    }

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
            // Dynamic Sphere上面へ半径分だけ侵入させます。
            // +X接線速度はDrag、正圧はPressure、侵入量はBuoyancyを同じCouplingで発生させます。
            m_Particles[0].Position = { 0.0f, 0.5f, 0.0f };
            m_Particles[0].Velocity = { 1.0f, -1.0f, 0.0f };
            m_Particles[0].Mass = 2.0f;
            m_Particles[0].Pressure = 10.0f;
        }
    }

    FluidCouplingBinding* GetFluidCouplingBinding() override
    {
        return &m_CouplingBinding;
    }

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

    std::vector<FluidParticle> particles(2u);
    // 遠方ParticleはCandidate統計へ残しつつBroad Phaseで厳密接触から除外されます。
    particles[1].Position = { 100.0f, 100.0f, 100.0f };
    TestFluidRigidBodyParticipant participant(particles);
    assert(fluidWorld.RegisterSimulationParticipant(participant) == true);

    constexpr float fixedDeltaTime = 0.1f;
    fluidWorld.StepSimulation(scene, rigidBodyWorld, fixedDeltaTime);

    assert(participant.StepCount == 1u);
    assert(IsNearlyEqual(participant.LastFixedDeltaTime, fixedDeltaTime));

    const RigidBodyComponent& resolvedBody = bodyEntity.GetComponent<RigidBodyComponent>();
    const FluidRigidBodyCouplingStatistics& statistics =
        fluidWorld.GetLastRigidBodyCouplingStatistics();

    // Participant Simulationで作った接触状態を同じFixed StepのFluidWorld Couplingが消費します。
    // 法線衝突・Drag・Pressure・BuoyancyがすべてWorld経由で実行されたことをCounterでも固定します。
    assert(statistics.DynamicBodyCount == 1u);
    assert(statistics.CandidatePairCount == 2u);
    assert(statistics.BroadPhaseRejectedPairCount == 1u);
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

    // DragはParticleの+X運動量をBodyへ、Pressure/法線反作用は-Y、Buoyancyは+YをBodyへ伝えます。
    // 各項の厳密値ではなく、World統合経路で双方向Impulseが実際にBodyへ届くことを確認します。
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

void RunFluidWorldRuntimeCouplingBindingTest()
{
    Scene scene;
    PhysicsSimulationWorld& simulationWorld = scene.GetPhysicsSimulationWorld();
    FluidWorld& fluidWorld = simulationWorld.GetFluidWorld();

    Entity staticSphereEntity = scene.CreateEntity("FluidWorld Runtime Coupling Binding Sphere");
    ColliderComponent collider{};
    collider.Type = ColliderType::Sphere;
    collider.Radius = 1.0f;
    collider.Restitution = 0.0f;
    staticSphereEntity.AddComponent<ColliderComponent>(collider);

    std::vector<FluidParticle> particles(1u);
    TestFluidSimulationParticipant participant(particles);
    assert(fluidWorld.RegisterSimulationParticipant(participant) == true);

    constexpr float fixedDeltaTime = 1.0f / 60.0f;
    fluidWorld.StepSimulation(scene, simulationWorld.GetRigidBodyWorld(), fixedDeltaTime);
    assert(IsNearlyEqual(particles[0].Position.x, 1.5f));

    // FluidWorldはBindingのコピーではなくParticipant所有Bindingへの非所有参照を保持します。
    // 登録後にCouplingを無効化した場合、再登録なしで次のfixed-stepから反映されることを固定します。
    participant.SetStaticColliderCouplingEnabled(false);
    fluidWorld.StepSimulation(scene, simulationWorld.GetRigidBodyWorld(), fixedDeltaTime);
    assert(IsNearlyEqual(particles[0].Position.x, 0.75f));
    assert(fluidWorld.GetLastStaticColliderCouplingStatistics().SupportedColliderCount == 0u);

    // Enable状態だけでなく設定値も同じBindingから毎step参照します。
    // Particle Radiusを0.5から0.25へ変更すると、Sphere半径1.0との合計境界はx=1.25になります。
    participant.SetStaticColliderParticleRadius(0.25f);
    participant.SetStaticColliderCouplingEnabled(true);
    fluidWorld.StepSimulation(scene, simulationWorld.GetRigidBodyWorld(), fixedDeltaTime);
    assert(IsNearlyEqual(particles[0].Position.x, 1.25f));

    assert(fluidWorld.UnregisterSimulationParticipant(participant) == true);
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

    std::vector<FluidParticle> particles(2u);
    particles[0].Position = { 5.0f, 0.0f, 0.0f };
    particles[1].Position = { 100.0f, 100.0f, 100.0f };

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
    assert(staticStatistics.CandidatePairCount == 2u);
    assert(staticStatistics.BroadPhaseRejectedPairCount == 1u);
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

    RunFluidWorldRigidBodyCouplingTest();
    RunFluidWorldMultipleBindingStatisticsTest();
    RunFluidWorldRuntimeCouplingBindingTest();
}

} // namespace Raven::ph::tests
