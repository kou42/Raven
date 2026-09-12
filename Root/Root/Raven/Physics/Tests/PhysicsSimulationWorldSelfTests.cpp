#include "Raven/Physics/PhysicsSimulationWorld.h"

#include <cassert>
#include <cmath>
#include <cstdint>

#include "Raven/Physics/SoftBody/SoftBodySolver.h"
#include "Raven/Core/CPUProfiler.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph::tests
{
namespace
{
class TestSoftBodySimulationParticipant final : public SoftBodySimulationParticipant
{
public:
    void SimulateSoftBody(float fixedDeltaTime) override
    {
        ++StepCount;
        LastFixedDeltaTime = fixedDeltaTime;
    }

    void SynchronizeSoftBodyOutput() override
    {
        ++SynchronizationCount;
    }

    uint32_t StepCount = 0u;
    uint32_t SynchronizationCount = 0u;
    float LastFixedDeltaTime = 0.0f;
};

class TestSoftBodySolverParticipant final : public SoftBodySimulationParticipant
{
public:
    explicit TestSoftBodySolverParticipant(SoftBodySolver& solver)
        : m_Solver(solver)
    {
    }

    void SimulateSoftBody(float fixedDeltaTime) override
    {
        m_Solver.Step(fixedDeltaTime);
    }

    void SynchronizeSoftBodyOutput() override
    {
    }

private:
    SoftBodySolver& m_Solver;
};

bool IsNearlyEqual(float left, float right)
{
    return std::abs(left - right) <= 1.0e-5f;
}

void RunScenePhysicsStepBudgetTest()
{
    // Sceneより先に生成し、Sceneの非所有RegistryよりParticipantを長生きさせます。
    TestSoftBodySimulationParticipant participant;
    Scene scene;
    auto& world = scene.GetPhysicsSimulationWorld().GetSoftBodyWorld();
    assert(world.RegisterSimulationParticipant(participant) == true);
    assert(scene.GetMaxPhysicsStepsPerFrame() == 4u);
    constexpr float step = 1.0f / 60.0f;

    scene.OnUpdate(0.5f * step);
    assert(participant.StepCount == 0u);
    assert(participant.SynchronizationCount == 0u);
    scene.OnUpdate(2.0f * step);
    assert(participant.StepCount == 2u);
    assert(participant.SynchronizationCount == 1u);

    CPUProfiler& profiler = CPUProfiler::Get();
    const bool wasEnabled = profiler.IsEnabled();
    profiler.SetEnabled(true);
    profiler.BeginFrame();
    scene.OnUpdate(10.0f * step);
    profiler.BeginFrame();
    assert(participant.StepCount == 6u);
    assert(participant.SynchronizationCount == 2u);
    assert(IsNearlyEqual(participant.LastFixedDeltaTime, step));

    // 実行4step / 破棄6step / 残り0.5stepが診断値にもそのまま出ることを確認します。
    const auto counterValue = [&](const char* name)
    {
        for (const CPUProfileCounter& counter : profiler.GetLastFrame().Counters)
        {
            if (counter.Name == name)
            {
                return counter.Value;
            }
        }
        assert(false);
        return -1.0;
    };
    assert(counterValue("Physics.FixedStep.Count") == 4.0);
    assert(std::abs(counterValue("Physics.FixedStep.DroppedMilliseconds") - 100.0) < 0.01);
    assert(std::abs(counterValue("Physics.FixedStep.AccumulatorMilliseconds") - 500.0 * step) < 0.01);
    profiler.SetEnabled(wasEnabled);

    // 過負荷の整数stepは持ち越さず、端数だけが次frameの積分へ参加します。
    scene.OnUpdate(0.6f * step);
    assert(participant.StepCount == 7u);
    assert(participant.SynchronizationCount == 3u);
    scene.SetMaxPhysicsStepsPerFrame(0u);
    assert(scene.GetMaxPhysicsStepsPerFrame() == 1u);
    scene.OnUpdate(5.0f * step);
    assert(participant.StepCount == 8u);
    scene.OnDestroy();
}
}

// PhysicsSimulationWorldがRigid Body / Soft Body Domainの入口を単一所有し、
// SoftBodyWorldがSolver参照とFixed Step Participantを重複なく非所有管理できることを確認します。
void RunPhysicsSimulationWorldSelfTests()
{
    RunScenePhysicsStepBudgetTest();
    PhysicsSimulationWorld simulationWorld;

    PhysicsWorld& rigidBodyWorld = simulationWorld.GetRigidBodyWorld();
    const PhysicsSimulationWorld& constSimulationWorld = simulationWorld;
    const PhysicsWorld& constRigidBodyWorld = constSimulationWorld.GetRigidBodyWorld();

    assert(&rigidBodyWorld == &constRigidBodyWorld);

    SoftBodyWorld& softBodyWorld = simulationWorld.GetSoftBodyWorld();
    const SoftBodyWorld& constSoftBodyWorld = constSimulationWorld.GetSoftBodyWorld();

    assert(&softBodyWorld == &constSoftBodyWorld);
    assert(softBodyWorld.GetRegisteredSolverCount() == 0u);
    assert(softBodyWorld.GetRegisteredSimulationParticipantCount() == 0u);

    SoftBodySolver firstSolver;
    SoftBodySolver secondSolver;

    assert(softBodyWorld.RegisterSolver(firstSolver) == true);
    assert(softBodyWorld.RegisterSolver(firstSolver) == false);
    assert(softBodyWorld.RegisterSolver(secondSolver) == true);
    assert(softBodyWorld.GetRegisteredSolverCount() == 2u);
    assert(softBodyWorld.ContainsSolver(firstSolver) == true);
    assert(softBodyWorld.ContainsSolver(secondSolver) == true);

    TestSoftBodySimulationParticipant firstParticipant;
    TestSoftBodySimulationParticipant secondParticipant;

    assert(softBodyWorld.RegisterSimulationParticipant(firstParticipant) == true);
    assert(softBodyWorld.RegisterSimulationParticipant(firstParticipant) == false);
    assert(softBodyWorld.RegisterSimulationParticipant(secondParticipant) == true);
    assert(softBodyWorld.GetRegisteredSimulationParticipantCount() == 2u);

    constexpr float fixedDeltaTime = 1.0f / 60.0f;
    softBodyWorld.Step(fixedDeltaTime);

    // 互換Stepは従来どおりSimulation -> Output同期を1回ずつ実行します。
    assert(firstParticipant.StepCount == 1u);
    assert(secondParticipant.StepCount == 1u);
    assert(firstParticipant.SynchronizationCount == 1u);
    assert(secondParticipant.SynchronizationCount == 1u);
    assert(firstParticipant.LastFixedDeltaTime == fixedDeltaTime);
    assert(secondParticipant.LastFixedDeltaTime == fixedDeltaTime);

    // catch-upを模した2回のSimulationでは、途中StateをOutputへ同期しません。
    softBodyWorld.StepSimulation(fixedDeltaTime);
    softBodyWorld.StepSimulation(fixedDeltaTime);
    assert(firstParticipant.StepCount == 3u);
    assert(secondParticipant.StepCount == 3u);
    assert(firstParticipant.SynchronizationCount == 1u);
    assert(secondParticipant.SynchronizationCount == 1u);

    // Application frameの最終Fixed Step後に1回だけ最新Stateを同期します。
    softBodyWorld.SynchronizeOutputs();
    assert(firstParticipant.SynchronizationCount == 2u);
    assert(secondParticipant.SynchronizationCount == 2u);

    assert(softBodyWorld.UnregisterSimulationParticipant(firstParticipant) == true);
    assert(softBodyWorld.UnregisterSimulationParticipant(firstParticipant) == false);
    assert(softBodyWorld.ContainsSimulationParticipant(firstParticipant) == false);

    // Unregister後は残ったParticipantだけがSimulation/同期されます。
    softBodyWorld.Step(fixedDeltaTime);
    assert(firstParticipant.StepCount == 3u);
    assert(firstParticipant.SynchronizationCount == 2u);
    assert(secondParticipant.StepCount == 4u);
    assert(secondParticipant.SynchronizationCount == 3u);

    assert(softBodyWorld.UnregisterSolver(firstSolver) == true);
    assert(softBodyWorld.UnregisterSolver(firstSolver) == false);
    assert(softBodyWorld.ContainsSolver(firstSolver) == false);

    softBodyWorld.Clear();
    assert(softBodyWorld.GetRegisteredSolverCount() == 0u);
    assert(softBodyWorld.GetRegisteredSimulationParticipantCount() == 0u);

    // ========================================================================
    // Rigid -> Soft Sphere Collider synchronization
    // ========================================================================
    // Source Rigid EntityはRigidBodyComponentを持たせず、PhysicsWorld::Stepで位置が変化しない
    // World-space Collider入力として使用します。Target Entityのuniform scale/translationを逆変換し、
    // 同じFixed Step内でSoftBody SolverのSphereへ反映されることを検証します。
    Scene scene;
    Entity rigidSphereEntity = scene.CreateEntity("Rigid Soft Sync Source");
    Entity secondRigidSphereEntity = scene.CreateEntity("Rigid Soft Sync Duplicate Source");
    Entity softBodyEntity = scene.CreateEntity("Rigid Soft Sync Target");

    TransformComponent& rigidTransform = rigidSphereEntity.GetComponent<TransformComponent>();
    rigidTransform.Position = { 14.0f, 6.0f, -2.0f };

    ColliderComponent rigidCollider{};
    rigidCollider.Type = ColliderType::Sphere;
    rigidCollider.Offset = { 2.0f, 0.0f, 0.0f };
    rigidCollider.Radius = 4.0f;
    rigidSphereEntity.AddComponent<ColliderComponent>(rigidCollider);

    TransformComponent& softTransform = softBodyEntity.GetComponent<TransformComponent>();
    softTransform.Position = { 10.0f, 2.0f, -6.0f };
    softTransform.Scale = { 2.0f, 2.0f, 2.0f };

    SoftBodySolver couplingSolver;
    const uint32_t couplingColliderIndex =
        couplingSolver.AddSphereCollider({ 0.0f, 0.0f, 0.0f }, 0.5f);

    PhysicsSimulationWorld& sceneSimulationWorld = scene.GetPhysicsSimulationWorld();
    RigidSoftSphereColliderBinding binding{};
    binding.SourceRigidEntity = rigidSphereEntity.GetHandle();
    binding.TargetSoftBodyEntity = softBodyEntity.GetHandle();
    binding.TargetSolver = &couplingSolver;
    binding.TargetColliderIndex = couplingColliderIndex;

    assert(sceneSimulationWorld.RegisterRigidSoftSphereColliderBinding(binding) == true);
    assert(sceneSimulationWorld.RegisterRigidSoftSphereColliderBinding(binding) == false);

    // 同じSoft Colliderへ別Rigid Sourceを割り当てても、同期先が同一なら登録を拒否します。
    // これによりRigid -> Softの上書き順依存と、Soft -> Rigid反作用の二重適用を防ぎます。
    RigidSoftSphereColliderBinding conflictingBinding = binding;
    conflictingBinding.SourceRigidEntity = secondRigidSphereEntity.GetHandle();
    assert(sceneSimulationWorld.RegisterRigidSoftSphereColliderBinding(conflictingBinding) == false);
    assert(sceneSimulationWorld.GetRigidSoftSphereColliderBindingCount() == 1u);

    sceneSimulationWorld.StepSimulation(scene, fixedDeltaTime);

    const std::vector<SoftBodySphereCollider>& synchronizedColliders =
        couplingSolver.GetSphereColliders();
    assert(couplingColliderIndex < synchronizedColliders.size());

    const SoftBodySphereCollider& synchronizedCollider =
        synchronizedColliders[couplingColliderIndex];

    // World center = (16, 6, -2), Target translation = (10, 2, -6), scale = 2
    // なのでSoft local center = (3, 2, 2)、radius = 4 / 2 = 2になります。
    assert(IsNearlyEqual(synchronizedCollider.Center.x, 3.0f));
    assert(IsNearlyEqual(synchronizedCollider.Center.y, 2.0f));
    assert(IsNearlyEqual(synchronizedCollider.Center.z, 2.0f));
    assert(IsNearlyEqual(synchronizedCollider.Radius, 2.0f));

    assert(sceneSimulationWorld.UnregisterRigidSoftSphereColliderBinding(
        couplingSolver,
        couplingColliderIndex) == true);
    assert(sceneSimulationWorld.UnregisterRigidSoftSphereColliderBinding(
        couplingSolver,
        couplingColliderIndex) == false);
    assert(sceneSimulationWorld.GetRigidSoftSphereColliderBindingCount() == 0u);

    // ========================================================================
    // Soft -> Rigid reaction impulse
    // ========================================================================
    // Rigid SphereをSoftBody local-space原点へ同期し、その内部にParticleを置きます。
    // Soft StepのSphere Constraintが生成した反作用をPhysicsSimulationWorldが直後に消費し、
    // Application Layerを経由せずDynamic RigidBodyの速度へ反映することを確認します。
    Scene reactionScene;
    Entity reactionRigidEntity = reactionScene.CreateEntity("Soft Rigid Reaction Source");
    Entity reactionSoftEntity = reactionScene.CreateEntity("Soft Rigid Reaction Target");

    TransformComponent& reactionRigidTransform =
        reactionRigidEntity.GetComponent<TransformComponent>();
    reactionRigidTransform.Position = { 10.0f, 0.0f, 0.0f };

    RigidBodyComponent reactionRigidBody{};
    reactionRigidBody.SetBodyType(BodyType::Dynamic);
    reactionRigidBody.SetMass(2.0f);
    reactionRigidBody.UseGravity = false;
    reactionRigidBody.AllowSleep = false;
    reactionRigidEntity.AddComponent<RigidBodyComponent>(reactionRigidBody);

    ColliderComponent reactionRigidCollider{};
    reactionRigidCollider.Type = ColliderType::Sphere;
    reactionRigidCollider.Radius = 2.0f;
    reactionRigidEntity.AddComponent<ColliderComponent>(reactionRigidCollider);

    TransformComponent& reactionSoftTransform =
        reactionSoftEntity.GetComponent<TransformComponent>();
    reactionSoftTransform.Position = { 10.0f, 0.0f, 0.0f };
    reactionSoftTransform.Scale = { 2.0f, 2.0f, 2.0f };

    SoftBodySolver reactionSolver;
    reactionSolver.SetGravity({ 0.0f, 0.0f, 0.0f });
    reactionSolver.AddParticle({ 0.5f, 0.0f, 0.0f }, 1.0f);
    const uint32_t reactionColliderIndex =
        reactionSolver.AddSphereCollider({ 0.0f, 0.0f, 0.0f }, 1.0f);

    TestSoftBodySolverParticipant reactionParticipant(reactionSolver);
    PhysicsSimulationWorld& reactionWorld = reactionScene.GetPhysicsSimulationWorld();
    assert(reactionWorld.GetSoftBodyWorld().RegisterSolver(reactionSolver) == true);
    assert(reactionWorld.GetSoftBodyWorld().RegisterSimulationParticipant(reactionParticipant) == true);

    RigidSoftSphereColliderBinding reactionBinding{};
    reactionBinding.SourceRigidEntity = reactionRigidEntity.GetHandle();
    reactionBinding.TargetSoftBodyEntity = reactionSoftEntity.GetHandle();
    reactionBinding.TargetSolver = &reactionSolver;
    reactionBinding.TargetColliderIndex = reactionColliderIndex;
    reactionBinding.ReactionEnabled = true;
    reactionBinding.ReactionImpulseScale = 0.5f;
    reactionBinding.MaximumReactionImpulse = 0.25f;

    assert(reactionWorld.RegisterRigidSoftSphereColliderBinding(reactionBinding) == true);
    reactionWorld.StepSimulation(reactionScene, fixedDeltaTime);

    const RigidBodyComponent& reactedRigidBody =
        reactionRigidEntity.GetComponent<RigidBodyComponent>();

    // ParticleはSphere中心から+X側にあるため、Sphere側反作用は-Xです。
    // Clamp後Impulseは最大0.25、Rigid mass=2なので|deltaV|は最大0.125です。
    assert(reactedRigidBody.LinearVelocity.x < 0.0f);
    assert(std::abs(reactedRigidBody.LinearVelocity.x) <= 0.125f + 1.0e-5f);
    assert(IsNearlyEqual(reactedRigidBody.LinearVelocity.y, 0.0f));
    assert(IsNearlyEqual(reactedRigidBody.LinearVelocity.z, 0.0f));

    // 接触点・ImpulseともX軸上なので r x J = 0 となり、角速度は発生しません。
    assert(IsNearlyEqual(reactedRigidBody.AngularVelocity.x, 0.0f));
    assert(IsNearlyEqual(reactedRigidBody.AngularVelocity.y, 0.0f));
    assert(IsNearlyEqual(reactedRigidBody.AngularVelocity.z, 0.0f));
}

}
