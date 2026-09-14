#include "Raven/Physics/PhysicsSimulationWorld.h"

#include <algorithm>
#include <cmath>

#include "Raven/Physics/SoftBody/SoftBodySolver.h"
#include "Raven/Physics/Thermal/ThermalSystem.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
bool HasSameRigidSoftSphereColliderDestination(
    const RigidSoftSphereColliderBinding& left,
    const RigidSoftSphereColliderBinding& right)
{
    return left.TargetSolver == right.TargetSolver
        && left.TargetColliderIndex == right.TargetColliderIndex;
}

math::Vec3 ClampMagnitude(const math::Vec3& value, float maximumMagnitude)
{
    if (maximumMagnitude <= 0.0f)
    {
        return value;
    }

    const float lengthSq = value.LengthSq();
    const float maximumMagnitudeSq = maximumMagnitude * maximumMagnitude;
    if (lengthSq <= maximumMagnitudeSq
        || lengthSq <= math::Epsilon * math::Epsilon)
    {
        return value;
    }

    return value * (maximumMagnitude / std::sqrt(lengthSq));
}

bool BuildInverseSoftBodyTransform(
    const TransformComponent& transform,
    math::Mat4& outInverseTransform,
    float& outUniformScale)
{
    constexpr float scaleEpsilon = 1.0e-8f;
    constexpr float uniformScaleTolerance = 1.0e-4f;

    const float scaleX = std::abs(transform.Scale.x);
    const float scaleY = std::abs(transform.Scale.y);
    const float scaleZ = std::abs(transform.Scale.z);

    if (scaleX <= scaleEpsilon
        || scaleY <= scaleEpsilon
        || scaleZ <= scaleEpsilon)
    {
        return false;
    }

    // Sphereを非一様ScaleのSoftBody local-spaceへ写すと楕円体になります。
    // 現在のSoftBody SolverはSphere Colliderだけを扱うため、形状を暗黙に歪めず
    // uniform scaleだけを同期対象とします。非一様Scale対応はEllipsoid等の形状追加時に行います。
    const float maximumScale = std::max(scaleX, std::max(scaleY, scaleZ));
    if (std::abs(scaleX - scaleY) > maximumScale * uniformScaleTolerance
        || std::abs(scaleX - scaleZ) > maximumScale * uniformScaleTolerance)
    {
        return false;
    }

    // TransformComponent::GetTransform() = T * Rx * Ry * Rz * S の逆変換を明示的に構築します。
    // Collider centerはpointとしてw=1で変換し、Radiusはuniform scaleだけで長さ変換します。
    const math::Mat4 inverseScale = math::Mat4::Scaling(math::Vec3{
        1.0f / transform.Scale.x,
        1.0f / transform.Scale.y,
        1.0f / transform.Scale.z
    });
    const math::Mat4 inverseRotationZ = math::Mat4::RotationZ(-transform.Rotation.z);
    const math::Mat4 inverseRotationY = math::Mat4::RotationY(-transform.Rotation.y);
    const math::Mat4 inverseRotationX = math::Mat4::RotationX(-transform.Rotation.x);
    const math::Mat4 inverseTranslation = math::Mat4::Translation(-transform.Position);

    outInverseTransform =
        inverseScale
        * inverseRotationZ
        * inverseRotationY
        * inverseRotationX
        * inverseTranslation;
    outUniformScale = scaleX;
    return true;
}
}

bool SoftBodyWorld::RegisterSolver(SoftBodySolver& solver)
{
    if (ContainsSolver(solver) == true)
    {
        return false;
    }

    m_Solvers.push_back(&solver);
    return true;
}

bool SoftBodyWorld::UnregisterSolver(SoftBodySolver& solver)
{
    const auto iterator = std::find(m_Solvers.begin(), m_Solvers.end(), &solver);
    if (iterator == m_Solvers.end())
    {
        return false;
    }

    m_Solvers.erase(iterator);
    return true;
}

bool SoftBodyWorld::RegisterSimulationParticipant(SoftBodySimulationParticipant& participant)
{
    if (ContainsSimulationParticipant(participant) == true)
    {
        return false;
    }

    m_SimulationParticipants.push_back(&participant);
    return true;
}

bool SoftBodyWorld::UnregisterSimulationParticipant(SoftBodySimulationParticipant& participant)
{
    const auto iterator = std::find(
        m_SimulationParticipants.begin(),
        m_SimulationParticipants.end(),
        &participant);

    if (iterator == m_SimulationParticipants.end())
    {
        return false;
    }

    m_SimulationParticipants.erase(iterator);
    return true;
}

void SoftBodyWorld::Clear()
{
    // RegistryはSolver/Participantを所有しないため、破棄は行わず参照だけを解除します。
    m_Solvers.clear();
    m_SimulationParticipants.clear();
}

void SoftBodyWorld::Step(float fixedDeltaTime)
{
    // 単発Stepの互換契約では、Simulation結果をその場で出力側へ反映します。
    StepSimulation(fixedDeltaTime);
    SynchronizeOutputs();
}

void SoftBodyWorld::StepSimulation(float fixedDeltaTime)
{
    // ========================================================================
    // SoftBody Simulation Phase
    // ========================================================================
    // Meshや具体的なCloth/Jelly型をPhysics Domainへ持ち込まず、登録済みParticipantだけを進めます。
    // catch-up時もここだけを複数回呼ぶことで、GPU更新を挟まずPhysics Stateを連続して積分できます。
    for (SoftBodySimulationParticipant* participant : m_SimulationParticipants)
    {
        if (participant == nullptr)
        {
            continue;
        }

        participant->SimulateSoftBody(fixedDeltaTime);
    }
}

void SoftBodyWorld::SynchronizeOutputs()
{
    // ========================================================================
    // Post-Simulation Output Synchronization
    // ========================================================================
    // 現在はMeshDeformerがこのhookを使ってParticle結果をMesh/GPUへ同期します。
    // Physics側は具体的な出力型を知らず、Participantの抽象境界だけを呼びます。
    // Sceneのcatch-up loop終了後に1回だけ呼ぶことで、途中Stateの不要なGPU uploadを避けます。
    for (SoftBodySimulationParticipant* participant : m_SimulationParticipants)
    {
        if (participant == nullptr)
        {
            continue;
        }

        participant->SynchronizeSoftBodyOutput();
    }
}

bool SoftBodyWorld::ContainsSolver(const SoftBodySolver& solver) const
{
    return std::find(m_Solvers.begin(), m_Solvers.end(), &solver) != m_Solvers.end();
}

bool SoftBodyWorld::ContainsSimulationParticipant(const SoftBodySimulationParticipant& participant) const
{
    return std::find(
        m_SimulationParticipants.begin(),
        m_SimulationParticipants.end(),
        &participant) != m_SimulationParticipants.end();
}

bool FluidWorld::RegisterSimulationParticipant(FluidSimulationParticipant& participant)
{
    if (ContainsSimulationParticipant(participant) == true)
    {
        return false;
    }

    FluidCouplingBinding* couplingBinding = participant.GetFluidCouplingBinding();
    if (couplingBinding != nullptr)
    {
        if (RegisterCouplingBinding(*couplingBinding) == false)
        {
            return false;
        }
    }

    m_SimulationParticipants.push_back(&participant);
    return true;
}

bool FluidWorld::UnregisterSimulationParticipant(FluidSimulationParticipant& participant)
{
    const auto iterator = std::find(
        m_SimulationParticipants.begin(),
        m_SimulationParticipants.end(),
        &participant);

    if (iterator == m_SimulationParticipants.end())
    {
        return false;
    }

    // Participantが所有するParticle配列やBindingより先に非所有参照を解除します。
    FluidCouplingBinding* couplingBinding = participant.GetFluidCouplingBinding();
    if (couplingBinding != nullptr && couplingBinding->Particles != nullptr)
    {
        UnregisterCouplingBinding(*couplingBinding->Particles);
    }

    m_SimulationParticipants.erase(iterator);
    return true;
}

bool FluidWorld::RegisterCouplingBinding(FluidCouplingBinding& binding)
{
    if (binding.Particles == nullptr)
    {
        return false;
    }

    if (ContainsCouplingBinding(*binding.Particles) == true)
    {
        // 1つのParticle配列へCouplingを二重適用すると位置補正・Impulseが重複するため、
        // Particle配列を一意なFluid Simulation境界として扱います。
        return false;
    }

    // BindingはParticipant側が所有します。コピーせず参照を保持することで、Runtimeに変更された
    // Coupling係数やEnable状態を次のfixed-stepからそのまま使用します。
    m_CouplingBindings.push_back(&binding);
    return true;
}

bool FluidWorld::UnregisterCouplingBinding(std::vector<FluidParticle>& particles)
{
    const auto iterator = std::find_if(
        m_CouplingBindings.begin(),
        m_CouplingBindings.end(),
        [&particles](const FluidCouplingBinding* binding)
        {
            return binding != nullptr && binding->Particles == &particles;
        });

    if (iterator == m_CouplingBindings.end())
    {
        return false;
    }

    m_CouplingBindings.erase(iterator);
    return true;
}

void FluidWorld::Step(float fixedDeltaTime)
{
    StepSimulation(fixedDeltaTime);
    SynchronizeOutputs();
}

void FluidWorld::StepSimulation(float fixedDeltaTime)
{
    // Sceneを持たないPhysics単体利用ではFluid数値計算だけを進めます。
    for (FluidSimulationParticipant* participant : m_SimulationParticipants)
    {
        if (participant == nullptr)
        {
            continue;
        }

        participant->SimulateFluid(fixedDeltaTime);
    }
}

void FluidWorld::StepSimulation(
    Scene& scene,
    PhysicsWorld& physicsWorld,
    float fixedDeltaTime)
{
    // ========================================================================
    // Fluid Domain Fixed Step
    // ========================================================================
    // 1. 各ParticipantのSPH/PBF/FLIP等の数値計算を完了
    // 2. 最新Particle位置に対してStatic Collider境界応答を解決
    // 3. Dynamic RigidBodyとの法線応答・Drag・Pressure・Buoyancyを双方向へ解決
    //
    // CouplingをParticipant::SimulateFluid()の外へ出すことで、Application/Debug Layerは
    // Scene/RigidBodyとのDomain間実行順序を知らず、FluidWorldだけがFixed Step境界を統括します。
    StepSimulation(fixedDeltaTime);
    ResolveCouplings(scene, physicsWorld, fixedDeltaTime);
    AccumulateCouplingMeasurement(fixedDeltaTime);
}

void FluidWorld::ResolveCouplings(
    Scene& scene,
    PhysicsWorld& physicsWorld,
    float fixedDeltaTime)
{
    for (FluidCouplingBinding* binding : m_CouplingBindings)
    {
        if (binding == nullptr || binding->Particles == nullptr)
        {
            continue;
        }

        if (binding->StaticColliderCouplingEnabled == true)
        {
            m_StaticColliderCoupling.SetSettings(binding->StaticColliderSettings);
            m_StaticColliderCoupling.ResolveScene(scene, *binding->Particles);
        }

        if (binding->RigidBodyCouplingEnabled == true)
        {
            m_RigidBodyCoupling.SetSettings(binding->RigidBodySettings);
            m_RigidBodyCoupling.ResolveScene(
                scene,
                physicsWorld,
                *binding->Particles,
                fixedDeltaTime);
        }
    }
}

void FluidWorld::AccumulateCouplingMeasurement(float fixedDeltaTime)
{
    if (m_CouplingMeasurementEnabled == false || fixedDeltaTime <= 0.0f)
    {
        return;
    }

    const FluidRigidBodyCouplingStatistics& statistics =
        GetLastRigidBodyCouplingStatistics();

    // World集約済みStatisticsをfixed-step終了時に1回だけ加算します。
    // Binding数が増えても二重計上せず、Couplingを無効化したStepも時間・Step数は進むため、
    // Preset間で同じ固定時間幅を基準に比較できます。
    m_CouplingMeasurement.ElapsedFixedTime += fixedDeltaTime;
    ++m_CouplingMeasurement.FixedStepCount;
    m_CouplingMeasurement.ResolvedContactCount += statistics.ResolvedContactCount;
    m_CouplingMeasurement.AppliedNormalImpulseCount += statistics.AppliedImpulseCount;
    m_CouplingMeasurement.AppliedDragImpulseCount += statistics.AppliedDragImpulseCount;
    m_CouplingMeasurement.AppliedPressureImpulseCount += statistics.AppliedPressureImpulseCount;
    m_CouplingMeasurement.AppliedBuoyancyImpulseCount += statistics.AppliedBuoyancyImpulseCount;
    m_CouplingMeasurement.TotalNormalImpulse += statistics.TotalNormalImpulse;
    m_CouplingMeasurement.TotalDragImpulse += statistics.TotalDragImpulse;
    m_CouplingMeasurement.TotalPressureImpulse += statistics.TotalPressureImpulse;
    m_CouplingMeasurement.TotalBuoyancyImpulse += statistics.TotalBuoyancyImpulse;
    m_CouplingMeasurement.TotalDisplacedFluidMass += statistics.TotalDisplacedFluidMass;
}

void FluidWorld::SynchronizeOutputs()
{
    // catch-up中の途中StateをRender/GPUへ送らず、Application frame末尾の最新状態だけを同期します。
    for (FluidSimulationParticipant* participant : m_SimulationParticipants)
    {
        if (participant == nullptr)
        {
            continue;
        }

        participant->SynchronizeFluidOutput();
    }
}

void FluidWorld::Clear()
{
    // Registryはいずれも非所有です。Binding/Particle/Participantを破棄せず参照だけを解除します。
    m_SimulationParticipants.clear();
    m_CouplingBindings.clear();
}

bool FluidWorld::ContainsSimulationParticipant(const FluidSimulationParticipant& participant) const
{
    return std::find(
        m_SimulationParticipants.begin(),
        m_SimulationParticipants.end(),
        &participant) != m_SimulationParticipants.end();
}

bool FluidWorld::ContainsCouplingBinding(const std::vector<FluidParticle>& particles) const
{
    return std::find_if(
        m_CouplingBindings.begin(),
        m_CouplingBindings.end(),
        [&particles](const FluidCouplingBinding* binding)
        {
            return binding != nullptr && binding->Particles == &particles;
        }) != m_CouplingBindings.end();
}

void PhysicsSimulationWorld::Step(Scene& scene, float fixedDeltaTime)
{
    // 単発Step利用側の互換性を維持し、Simulationと出力同期を連続して完了させます。
    StepSimulation(scene, fixedDeltaTime);
    SynchronizeOutputs();
}

void PhysicsSimulationWorld::StepSimulation(Scene& scene, float fixedDeltaTime)
{
    // ========================================================================
    // Rigid / Fluid / Soft / Thermal fixed-step ordering
    // ========================================================================
    // 1. Rigid Bodyを進め、Collision Detection / Contact Solverまで完了させる
    // 2. Fluid数値計算とRigid/Collider Couplingを同じFixed Step内で完了させる
    // 3. 最新Rigid ColliderをSoftBody local-spaceへ同期
    // 4. Soft Bodyを進めてCollision ConstraintとReaction Feedbackを確定
    // 5. そのSoft Stepで生成された反作用ImpulseをRigid Bodyへ返す
    // 6. ECSからThermal Registryを再構築し、同じRigid Stepで得たContact Manifoldを熱接触へ変換
    // 7. Thermal Domainの熱伝導を同じFixed Step幅で進める
    //
    // Fluid -> Rigid反作用はFluid Step内でRigid速度へ反映され、次Fixed StepのRigid積分から利用されます。
    m_RigidBodyWorld.Step(scene, fixedDeltaTime);
    m_FluidWorld.StepSimulation(scene, m_RigidBodyWorld, fixedDeltaTime);
    SynchronizeRigidBodyCollidersToSoftBody(scene);
    m_SoftBodyWorld.StepSimulation(fixedDeltaTime);
    ApplySoftBodyReactionsToRigidBodies(scene);

    ThermalSystem::SynchronizeWorld(scene);
    ThermalSystem::AppendRigidBodyContacts(scene, m_RigidBodyWorld.GetContactManifolds());
    m_ThermalWorld.Step(fixedDeltaTime);
}

bool PhysicsSimulationWorld::RegisterRigidSoftSphereColliderBinding(
    const RigidSoftSphereColliderBinding& binding)
{
    if (binding.TargetSolver == nullptr)
    {
        return false;
    }

    const auto iterator = std::find_if(
        m_RigidSoftSphereColliderBindings.begin(),
        m_RigidSoftSphereColliderBindings.end(),
        [&binding](const RigidSoftSphereColliderBinding& registeredBinding)
        {
            return HasSameRigidSoftSphereColliderDestination(registeredBinding, binding);
        });

    if (iterator != m_RigidSoftSphereColliderBindings.end())
    {
        // 1つのSoft Sphere Colliderは1つのRigid Sourceだけが所有します。
        // 複数Sourceを許可するとRigid -> Soft同期で後勝ちになり、Soft -> Rigid反作用も
        // 同じColliderのFeedbackを複数Rigidへ返せるため、登録時点で曖昧なPairを拒否します。
        return false;
    }

    m_RigidSoftSphereColliderBindings.push_back(binding);
    return true;
}

bool PhysicsSimulationWorld::UnregisterRigidSoftSphereColliderBinding(
    SoftBodySolver& targetSolver,
    uint32_t targetColliderIndex)
{
    const auto iterator = std::find_if(
        m_RigidSoftSphereColliderBindings.begin(),
        m_RigidSoftSphereColliderBindings.end(),
        [&targetSolver, targetColliderIndex](const RigidSoftSphereColliderBinding& binding)
        {
            return binding.TargetSolver == &targetSolver
                && binding.TargetColliderIndex == targetColliderIndex;
        });

    if (iterator == m_RigidSoftSphereColliderBindings.end())
    {
        return false;
    }

    m_RigidSoftSphereColliderBindings.erase(iterator);
    return true;
}

void PhysicsSimulationWorld::ClearRigidSoftSphereColliderBindings()
{
    // BindingはEntity/Solverを所有しません。SceneやDeformer破棄前に参照だけを解除します。
    m_RigidSoftSphereColliderBindings.clear();
}

void PhysicsSimulationWorld::SynchronizeRigidBodyCollidersToSoftBody(Scene& scene)
{
    for (const RigidSoftSphereColliderBinding& binding : m_RigidSoftSphereColliderBindings)
    {
        if (binding.TargetSolver == nullptr
            || scene.IsEntityAlive(binding.SourceRigidEntity) == false
            || scene.IsEntityAlive(binding.TargetSoftBodyEntity) == false)
        {
            continue;
        }

        const TransformComponent* rigidTransform =
            scene.TryGetComponent<TransformComponent>(binding.SourceRigidEntity.m_Index);
        const ColliderComponent* rigidCollider =
            scene.TryGetComponent<ColliderComponent>(binding.SourceRigidEntity.m_Index);
        const TransformComponent* softBodyTransform =
            scene.TryGetComponent<TransformComponent>(binding.TargetSoftBodyEntity.m_Index);

        if (rigidTransform == nullptr
            || rigidCollider == nullptr
            || softBodyTransform == nullptr
            || rigidCollider->Type != ColliderType::Sphere)
        {
            continue;
        }

        math::Mat4 inverseSoftBodyTransform{};
        float softBodyUniformScale = 1.0f;
        if (BuildInverseSoftBodyTransform(
            *softBodyTransform,
            inverseSoftBodyTransform,
            softBodyUniformScale) == false)
        {
            continue;
        }

        // 既存Rigid/Soft Demoと同じCollider center契約を維持し、Transform PositionへOffsetを加えた
        // World centerをSoftBody local-spaceへ逆変換します。
        const math::Vec3 worldCenter = rigidTransform->Position + rigidCollider->Offset;
        const math::Vec4 localCenter4 =
            inverseSoftBodyTransform * math::Vec4{ worldCenter, 1.0f };
        const math::Vec3 localCenter{
            localCenter4.x,
            localCenter4.y,
            localCenter4.z
        };
        const float localRadius = rigidCollider->Radius / softBodyUniformScale;

        binding.TargetSolver->SetSphereCollider(
            binding.TargetColliderIndex,
            localCenter,
            localRadius);
    }
}

void PhysicsSimulationWorld::ApplySoftBodyReactionsToRigidBodies(Scene& scene)
{
    for (const RigidSoftSphereColliderBinding& binding : m_RigidSoftSphereColliderBindings)
    {
        if (binding.ReactionEnabled == false
            || binding.TargetSolver == nullptr
            || scene.IsEntityAlive(binding.SourceRigidEntity) == false
            || scene.IsEntityAlive(binding.TargetSoftBodyEntity) == false)
        {
            continue;
        }

        const TransformComponent* softBodyTransform =
            scene.TryGetComponent<TransformComponent>(binding.TargetSoftBodyEntity.m_Index);
        if (softBodyTransform == nullptr)
        {
            continue;
        }

        // Rigid -> Soft同期と同じShape契約を使います。非一様ScaleではSphereがlocal-spaceで
        // EllipsoidになるためCollider同期自体を行わず、古いFeedbackを誤ってRigidへ返すことも避けます。
        math::Mat4 unusedInverseTransform{};
        float unusedUniformScale = 1.0f;
        if (BuildInverseSoftBodyTransform(
            *softBodyTransform,
            unusedInverseTransform,
            unusedUniformScale) == false)
        {
            continue;
        }

        const std::vector<SoftBodySphereCollider>& sphereColliders =
            binding.TargetSolver->GetSphereColliders();
        if (binding.TargetColliderIndex >= sphereColliders.size())
        {
            continue;
        }

        const SoftBodySphereCollider& softSphere = sphereColliders[binding.TargetColliderIndex];
        if (softSphere.ContactCount == 0u)
        {
            continue;
        }

        // AccumulatedReactionImpulseと平均Contact PointはSolver local-spaceです。
        // Vectorはw=0、Pointはw=1でEntity Transformを適用し、translationがImpulseへ混ざらないようにします。
        const math::Mat4 softBodyWorldTransform = softBodyTransform->GetTransform();
        const math::Vec4 worldReactionImpulse4 = softBodyWorldTransform
            * math::Vec4{ softSphere.AccumulatedReactionImpulse, 0.0f };
        math::Vec3 worldReactionImpulse{
            worldReactionImpulse4.x,
            worldReactionImpulse4.y,
            worldReactionImpulse4.z
        };
        worldReactionImpulse *= binding.ReactionImpulseScale;
        worldReactionImpulse = ClampMagnitude(
            worldReactionImpulse,
            binding.MaximumReactionImpulse);

        if (worldReactionImpulse.LengthSq() <= math::Epsilon * math::Epsilon)
        {
            continue;
        }

        const math::Vec4 worldContactPoint4 = softBodyWorldTransform
            * math::Vec4{ softSphere.GetAverageContactPoint(), 1.0f };
        const math::Vec3 worldContactPoint{
            worldContactPoint4.x,
            worldContactPoint4.y,
            worldContactPoint4.z
        };

        // AddImpulseAtPoint()側がDynamic Body判定、InverseMass判定、Wake-up、r x Jによる角Impulseを
        // 共通処理するため、Coupling側ではRigidBody内部状態を直接変更しません。
        m_RigidBodyWorld.AddImpulseAtPoint(
            scene,
            Entity(binding.SourceRigidEntity, &scene),
            worldReactionImpulse,
            worldContactPoint);
    }
}

void PhysicsSimulationWorld::SynchronizeOutputs()
{
    m_FluidWorld.SynchronizeOutputs();
    m_SoftBodyWorld.SynchronizeOutputs();
}

PhysicsWorld& PhysicsSimulationWorld::GetRigidBodyWorld()
{
    return m_RigidBodyWorld;
}

const PhysicsWorld& PhysicsSimulationWorld::GetRigidBodyWorld() const
{
    return m_RigidBodyWorld;
}

FluidWorld& PhysicsSimulationWorld::GetFluidWorld()
{
    return m_FluidWorld;
}

const FluidWorld& PhysicsSimulationWorld::GetFluidWorld() const
{
    return m_FluidWorld;
}

SoftBodyWorld& PhysicsSimulationWorld::GetSoftBodyWorld()
{
    return m_SoftBodyWorld;
}

const SoftBodyWorld& PhysicsSimulationWorld::GetSoftBodyWorld() const
{
    return m_SoftBodyWorld;
}

ThermalWorld& PhysicsSimulationWorld::GetThermalWorld()
{
    return m_ThermalWorld;
}

const ThermalWorld& PhysicsSimulationWorld::GetThermalWorld() const
{
    return m_ThermalWorld;
}

}
