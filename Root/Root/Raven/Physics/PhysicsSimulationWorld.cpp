#include "Raven/Physics/PhysicsSimulationWorld.h"

#include <algorithm>
#include <cmath>

#include "Raven/Physics/SoftBody/SoftBodySolver.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
bool IsSameRigidSoftSphereColliderBinding(
    const RigidSoftSphereColliderBinding& left,
    const RigidSoftSphereColliderBinding& right)
{
    return left.SourceRigidEntity == right.SourceRigidEntity
        && left.TargetSoftBodyEntity == right.TargetSoftBodyEntity
        && left.TargetSolver == right.TargetSolver
        && left.TargetColliderIndex == right.TargetColliderIndex;
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

void PhysicsSimulationWorld::Step(Scene& scene, float fixedDeltaTime)
{
    // 単発Step利用側の互換性を維持し、Simulationと出力同期を連続して完了させます。
    StepSimulation(scene, fixedDeltaTime);
    SynchronizeOutputs();
}

void PhysicsSimulationWorld::StepSimulation(Scene& scene, float fixedDeltaTime)
{
    // ========================================================================
    // Rigid -> Soft fixed-step ordering
    // ========================================================================
    // 1. Rigid Bodyを進めてWorld Transformを確定
    // 2. 最新Rigid ColliderをSoftBody local-spaceへ同期
    // 3. Soft Bodyを進める
    //
    // これにより従来Demo Layer::OnUpdate()で行っていた「Scene Physics終了後に同期し、次frameで使用」
    // という1frame遅延をなくし、同じFixed StepのRigid結果をそのままSoft Collisionへ入力できます。
    m_RigidBodyWorld.Step(scene, fixedDeltaTime);
    SynchronizeRigidBodyCollidersToSoftBody(scene);
    m_SoftBodyWorld.StepSimulation(fixedDeltaTime);
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
            return IsSameRigidSoftSphereColliderBinding(registeredBinding, binding);
        });

    if (iterator != m_RigidSoftSphereColliderBindings.end())
    {
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

void PhysicsSimulationWorld::SynchronizeOutputs()
{
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

SoftBodyWorld& PhysicsSimulationWorld::GetSoftBodyWorld()
{
    return m_SoftBodyWorld;
}

const SoftBodyWorld& PhysicsSimulationWorld::GetSoftBodyWorld() const
{
    return m_SoftBodyWorld;
}

}
