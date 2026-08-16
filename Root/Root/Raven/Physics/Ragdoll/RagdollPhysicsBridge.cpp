// Raven/Physics/Ragdoll/RagdollPhysicsBridge.cpp
#include "Raven/Physics/Ragdoll/RagdollPhysicsBridge.h"

#include <cmath>
#include <string>
#include <utility>

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
        // Ragdoll Body -> Capsule Collider
        // ====================================================================
        // RagdollBodyDefinitionのRadius / HalfLengthは最初からCapsule向けの意味で定義しているため、
        // Collider側にも同じ値をそのまま渡します。CapsuleはBoneローカル+Yを長軸とし、
        // Transformの回転に追従します。
        //
        // 以前のBox近似では四隅が床や隣接Bodyへ引っ掛かりやすく、肩・肘・膝などで
        // 不自然なContactが生まれていました。Capsule化により四肢の断面が連続曲面となり、
        // Ragdollの回転運動と接触がより自然になります。
        collider.Type = ColliderType::Capsule;
        collider.Radius = bodyDefinition->Radius;
        collider.HalfLength = bodyDefinition->HalfLength;
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

    // ========================================================================
    // Joint Parent / Child Bodyの自己衝突除外
    // ========================================================================
    // Ragdollでは関節で直接つながるBody同士が常に近接するため、通常のCollider衝突を
    // 同時に解くと「Jointは近づける / Contactは離す」という競合が発生します。
    // その結果、肩・肘・膝などが震えたり、Constraint Projectionが不安定になります。
    //
    // そこでDefinitionのJointごとにParent/Child Physics Entityを解決し、PhysicsWorldの
    // Ignore Pairへ登録します。隣接していないBody同士の自己衝突は残るため、腕が胴体を
    // 貫通するようなケースは従来通りContact Solverで処理されます。
    ph::PhysicsWorld& physicsWorld = scene.GetPhysicsWorld();
    for (const RagdollJointDefinition& jointDefinition : definition.Joints)
    {
        const BoneIndex parentBone = skeleton->FindBone(jointDefinition.ParentBoneName);
        const BoneIndex childBone = skeleton->FindBone(jointDefinition.ChildBoneName);
        if (parentBone == InvalidBoneIndex || childBone == InvalidBoneIndex)
        {
            return SetError(
                errorMessage,
                "Ragdoll Jointの衝突除外対象Boneを解決できません: "
                + jointDefinition.ParentBoneName + " -> " + jointDefinition.ChildBoneName);
        }

        const Entity parentEntity = FindEntity(parentBone);
        const Entity childEntity = FindEntity(childBone);
        if (static_cast<bool>(parentEntity) == false
            || static_cast<bool>(childEntity) == false)
        {
            return SetError(
                errorMessage,
                "Ragdoll Jointに対応するPhysics Entityを解決できません: "
                + jointDefinition.ParentBoneName + " -> " + jointDefinition.ChildBoneName);
        }

        physicsWorld.AddIgnoreCollisionPair(parentEntity, childEntity);
    }

    return true;
}

void RagdollPhysicsBridge::DestroyBodies(Scene& scene)
{
    ph::PhysicsWorld& physicsWorld = scene.GetPhysicsWorld();

    for (const RagdollPhysicsBodyBinding& binding : m_Bindings)
    {
        // Entity破棄前に、このBodyを含むIgnore Pairを明示的に解除します。
        // BroadPhase側にも孤立Pairの自動掃除がありますが、Bridgeが生成した設定は
        // Bridge自身で片付けることでLifetimeの責務を明確にします。
        physicsWorld.RemoveIgnoreCollisionPairsForEntity(binding.BodyEntity);

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
