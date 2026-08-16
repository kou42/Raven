// Raven/Gltf/SkinnedRagdollRuntime.h
#pragma once

#include <cstddef>
#include <string>

#include "Raven/Physics/Ragdoll/RagdollConstraintSolver.h"
#include "Raven/Physics/Ragdoll/RagdollPhysicsBridge.h"
#include "Raven/Physics/Ragdoll/RagdollRuntime.h"

namespace Raven
{
class Scene;

namespace Gltf
{

class SkinnedMeshRuntimeAsset;

class SkinnedRagdollRuntime
{
public:
    bool Attach(
        std::size_t skinIndex,
        SkinnedMeshRuntimeAsset& targetAsset,
        const RagdollDefinition& definition,
        std::string* errorMessage = nullptr);

    bool EnterRagdoll(std::string* errorMessage = nullptr);
    bool InitializeConstraints(std::string* errorMessage = nullptr);

    // EnterRagdoll()後のBody PoseからScene上にDynamic RigidBody / Collider Entityを生成します。
    // SceneのPhysicsWorldはECS Componentを直接走査するため、生成後は通常のPhysics Step対象になります。
    bool CreatePhysicsBodies(
        Scene& scene,
        const std::string& entityNamePrefix = "Ragdoll",
        std::string* errorMessage = nullptr);

    void DestroyPhysicsBodies(Scene& scene);

    bool SetBodyState(
        BoneIndex boneIndex,
        const RagdollBodyState& state,
        std::string* errorMessage = nullptr);

    bool SetBodyState(
        const std::string& boneName,
        const RagdollBodyState& state,
        std::string* errorMessage = nullptr);

    bool SolveConstraints(
        const RagdollConstraintSolverSettings& settings = {},
        std::string* errorMessage = nullptr);

    // PhysicsWorld::Step()後のScene RigidBody状態をRagdoll Runtimeへ取り込みます。
    bool SyncPhysicsToRagdoll(
        const Scene& scene,
        std::string* errorMessage = nullptr);

    // Constraint Projection後のRagdoll Body PoseをScene RigidBodyへ戻します。
    // Position / Orientationを同期し、次FrameのPhysics開始状態をJoint制約と一致させます。
    bool SyncRagdollToPhysics(
        Scene& scene,
        bool syncVelocities = false,
        std::string* errorMessage = nullptr);

    bool ApplyPose(
        float weight = 1.0f,
        std::string* errorMessage = nullptr);

    // PhysicsWorld::Step()後に呼ぶ統合入口です。
    // Physics -> Ragdoll -> Joint Constraint -> Physics再同期 -> Skeleton -> Mesh の順で進めます。
    bool UpdatePhysicsDrivenMesh(
        Scene& scene,
        float deltaTime,
        const RagdollConstraintSolverSettings& settings = {},
        float weight = 1.0f,
        std::string* errorMessage = nullptr);

    bool UpdateConstrainedMesh(
        float deltaTime,
        const RagdollConstraintSolverSettings& settings = {},
        float weight = 1.0f,
        std::string* errorMessage = nullptr);

    bool UpdateMesh(
        float deltaTime,
        float weight = 1.0f,
        std::string* errorMessage = nullptr);

    RagdollRuntime& GetRagdoll()
    {
        return m_Ragdoll;
    }

    const RagdollRuntime& GetRagdoll() const
    {
        return m_Ragdoll;
    }

    RagdollConstraintSolver& GetConstraintSolver()
    {
        return m_ConstraintSolver;
    }

    const RagdollConstraintSolver& GetConstraintSolver() const
    {
        return m_ConstraintSolver;
    }

    RagdollPhysicsBridge& GetPhysicsBridge()
    {
        return m_PhysicsBridge;
    }

    const RagdollPhysicsBridge& GetPhysicsBridge() const
    {
        return m_PhysicsBridge;
    }

private:
    std::size_t m_SkinIndex = 0u;
    SkinnedMeshRuntimeAsset* m_TargetAsset = nullptr;
    RagdollRuntime m_Ragdoll{};
    RagdollConstraintSolver m_ConstraintSolver{};
    RagdollPhysicsBridge m_PhysicsBridge{};
};

} // namespace Gltf
} // namespace Raven
