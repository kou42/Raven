// Raven/Physics/Ragdoll/RagdollPhysicsBridge.cpp
#include "Raven/Physics/Ragdoll/RagdollPhysicsBridge.h"

#include <cmath>
#include <string>

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{
namespace
{

bool SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

bool IsFinite(const math::Vec3& value)
{
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
}

} // namespace

const RagdollBodyDefinition* RagdollPhysicsBridge::FindBodyDefinition(
    const RagdollDefinition& definition,
    BoneIndex boneIndex,
    const Skeleton& skeleton) const
{
    if (boneIndex >= skeleton.GetBoneCount())
    {
        return nullptr;
    }

    const std::string& boneName = skeleton.GetBone(boneIndex).Name;
    for (const RagdollBodyDefinition& bodyDefinition : definition.Bodies)
    {
        if (bodyDefinition.BoneName == boneName)
        {
            return &bodyDefinition;
        }
    }

    return nullptr;
}

bool RagdollPhysicsBridge::CreateBodies(
    Scene& scene,
    RagdollRuntime& ragdoll,
    const std::string& entityNamePrefix,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (ragdoll.IsBuilt() == false)
    {
        return SetError(errorMessage, "Physics Body生成にはBuild済みRagdollRuntimeが必要です");
    }
    if (m_Bindings.empty() == false)
    {
        return SetError(errorMessage, "Ragdoll Physics Bodyは既に生成されています");
    }

    const Skeleton* skeleton = ragdoll.GetSkeleton();
    if (skeleton == nullptr)
    {
        return SetError(errorMessage, "Ragdoll Skeletonがnullptrです");
    }

    const RagdollDefinition& definition = ragdoll.GetDefinition();
    std::vector<RagdollPhysicsBodyBinding> bindings;
    bindings.reserve(ragdoll.GetBodies().size());

    for (const RagdollBodyState& bodyState : ragdoll.GetBodies())
    {
        const RagdollBodyDefinition* bodyDefinition = FindBodyDefinition(
            definition,
            bodyState.Bone,
            *skeleton);
        if (bodyDefinition == nullptr)
        {
            return SetError(errorMessage, "Ragdoll BodyDefinitionをBoneIndexから解決できません");
        }

        if (IsFinite(bodyState.Position) == false
            || std::isfinite(bodyState.Rotation.LengthSq()) == false
            || bodyState.Rotation.LengthSq() <= math::Epsilon)
        {
            return SetError(errorMessage, "Ragdoll Body初期Poseが不正です: " + bodyDefinition->BoneName);
        }

        Entity entity = scene.CreateEntity(entityNamePrefix + "_" + bodyDefinition->BoneName);
        if (static_cast<bool>(entity) == false)
        {
            return SetError(errorMessage, "Ragdoll Physics Entityの生成に失敗しました");
        }

        TransformComponent& transform = entity.GetComponent<TransformComponent>();
        transform.Position = bodyState.Position;
        transform.Rotation = bodyState.Rotation.Normalized().ToEulerXYZ();

        RigidBodyComponent& rigidBody = entity.AddComponent<RigidBodyComponent>();
        rigidBody.SetBodyType(BodyType::Dynamic);
        rigidBody.SetMass(bodyDefinition->Mass);
        rigidBody.LinearVelocity = bodyState.LinearVelocity;
        rigidBody.AngularVelocity = bodyState.AngularVelocity;
        rigidBody.Orientation = bodyState.Rotation.Normalized();
        rigidBody.OrientationInitialized = true;
        rigidBody.UseGravity = true;
        rigidBody.AllowSleep = false;
        rigidBody.IsSleeping = false;

        ColliderComponent& collider = entity.AddComponent<ColliderComponent>();

        // ====================================================================
        // Capsule未実装時のBox近似
        // ====================================================================
        // RagdollBodyDefinitionはRadius / HalfLengthというCapsule向けの意味を持っていますが、
        // 現在のColliderComponentはSphere / Box / Planeのみです。
        // ここではBoneのローカル+Yを長軸とする縦長Boxへ近似し、PhysicsWorldの既存OBB衝突を
        // そのまま利用します。Capsule Collider追加後はこの区画だけを差し替えます。
        collider.Type = ColliderType::Box;
        collider.HalfExtents = math::Vec3{
            bodyDefinition->Radius,
            bodyDefinition->HalfLength + bodyDefinition->Radius,
            bodyDefinition->Radius
        };
        collider.Restitution = 0.0f;
        collider.StaticFriction = 0.7f;
        collider.DynamicFriction = 0.5f;
        collider.IsTrigger = false;

        RagdollPhysicsBodyBinding binding{};
        binding.Bone = bodyState.Bone;
        binding.BodyEntity = entity;
        bindings.emplace_back(binding);
    }

    m_Bindings = std::move(bindings);
    return true;
}

void RagdollPhysicsBridge::DestroyBodies(Scene& scene)
{
    for (const RagdollPhysicsBodyBinding& binding : m_Bindings)
    {
        if (scene.IsEntityAlive(binding.BodyEntity))
        {
            scene.QueueDestroyEntity(binding.BodyEntity);
        }
    }

    m_Bindings.clear();
}

Entity RagdollPhysicsBridge::FindEntity(BoneIndex boneIndex) const
{
    for (const RagdollPhysicsBodyBinding& binding : m_Bindings)
    {
        if (binding.Bone == boneIndex)
        {
            return binding.BodyEntity;
        }
    }

    return Entity{};
}

bool RagdollPhysicsBridge::SyncRagdollToPhysics(
    Scene& scene,
    const RagdollRuntime& ragdoll,
    bool syncVelocities,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_Bindings.empty())
    {
        return SetError(errorMessage, "Ragdoll Physics Bodyが生成されていません");
    }

    for (const RagdollPhysicsBodyBinding& binding : m_Bindings)
    {
        if (scene.IsEntityAlive(binding.BodyEntity) == false)
        {
            return SetError(errorMessage, "Ragdoll Physics Entityが破棄されています");
        }

        const RagdollBodyState* bodyState = ragdoll.FindBody(binding.Bone);
        if (bodyState == nullptr)
        {
            return SetError(errorMessage, "Bindingに対応するRagdoll Bodyがありません");
        }

        TransformComponent* transform = scene.TryGetComponent<TransformComponent>(
            binding.BodyEntity.GetIndex());
        RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(
            binding.BodyEntity.GetIndex());
        if (transform == nullptr || rigidBody == nullptr)
        {
            return SetError(errorMessage, "Ragdoll Physics Entityの必須Componentがありません");
        }

        transform->Position = bodyState->Position;
        transform->Rotation = bodyState->Rotation.Normalized().ToEulerXYZ();
        rigidBody->Orientation = bodyState->Rotation.Normalized();
        rigidBody->OrientationInitialized = true;
        rigidBody->IsSleeping = false;
        rigidBody->SleepTimer = 0.0f;

        if (syncVelocities)
        {
            rigidBody->LinearVelocity = bodyState->LinearVelocity;
            rigidBody->AngularVelocity = bodyState->AngularVelocity;
        }
    }

    return true;
}

bool RagdollPhysicsBridge::SyncPhysicsToRagdoll(
    const Scene& scene,
    RagdollRuntime& ragdoll,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_Bindings.empty())
    {
        return SetError(errorMessage, "Ragdoll Physics Bodyが生成されていません");
    }

    for (const RagdollPhysicsBodyBinding& binding : m_Bindings)
    {
        if (scene.IsEntityAlive(binding.BodyEntity) == false)
        {
            return SetError(errorMessage, "Ragdoll Physics Entityが破棄されています");
        }

        const TransformComponent* transform = scene.TryGetComponent<TransformComponent>(
            binding.BodyEntity.GetIndex());
        const RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(
            binding.BodyEntity.GetIndex());
        if (transform == nullptr || rigidBody == nullptr)
        {
            return SetError(errorMessage, "Ragdoll Physics Entityの必須Componentがありません");
        }

        RagdollBodyState state{};
        state.Bone = binding.Bone;
        state.Position = transform->Position;
        state.Rotation = rigidBody->OrientationInitialized
            ? rigidBody->Orientation.Normalized()
            : math::Quat::FromEulerXYZ(
                transform->Rotation.x,
                transform->Rotation.y,
                transform->Rotation.z).Normalized();
        state.LinearVelocity = rigidBody->LinearVelocity;
        state.AngularVelocity = rigidBody->AngularVelocity;

        if (ragdoll.SetBodyState(binding.Bone, state, errorMessage) == false)
        {
            return false;
        }
    }

    return true;
}

} // namespace Raven
