// Raven/Scene/Components.h
#pragma once

#include <memory>
#include <string>

#include "Raven/Math/Math.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathQuatanion.h"

namespace Raven
{

class Mesh;
class Material;
class MeshDeformationInstance;

struct TagComponent
{
    std::string Tag = "Entity";
};

struct TransformComponent
{
    math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Rotation{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Scale{ 1.0f, 1.0f, 1.0f };

    math::Mat4 GetTransform() const;
};

struct MeshRendererComponent
{
    std::shared_ptr<Mesh> Mesh = nullptr;
    std::shared_ptr<Material> Material = nullptr;

    bool IsValid() const
    {
        return Mesh && Material;
    }
};

// ============================================================================
// MeshDeformationComponent
// ============================================================================
// Entityが「変形可能なMesh」を持つことだけをScene/ECSへ公開するComponentです。
//
// 重要なのは、Scene側がWave / Skeletal / Morph / SoftBodyなどの具体的なDeformer型を
// 知らないことです。具体的な変形アルゴリズムとMeshの組はMeshDeformationInstanceへ隠し、
// Sceneは毎フレームInstanceを更新するだけにします。
//
// shared_ptrを使う理由:
// ComponentStorageはComponentを移動・再配置する可能性がありますが、Instance内部には
// unique ownershipのDeformerが含まれます。Component自体を軽量な共有Handleにすることで、
// ECS Storageの都合とDeformerの所有権を分離できます。
struct MeshDeformationComponent
{
    std::shared_ptr<MeshDeformationInstance> Instance = nullptr;
    bool Enabled = true;

    bool IsValid() const
    {
        return Instance != nullptr;
    }
};

enum class BodyType
{
    Static,
    Dynamic,
    Kinematic
};

struct RigidBodyComponent
{
    BodyType Type = BodyType::Dynamic;

    float Mass = 1.0f;
    float InverseMass = 1.0f;

    math::Vec3 LinearVelocity{ 0.0f, 0.0f, 0.0f };
    math::Vec3 AngularVelocity{ 0.0f, 0.0f, 0.0f };

    math::Vec3 Force{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Torque{ 0.0f, 0.0f, 0.0f };

    math::Quat Orientation = math::Quat::Identity();
    bool OrientationInitialized = false;

    float LinearDamping = 0.01f;
    float AngularDamping = 0.01f;

    bool UseGravity = true;
    bool AllowSleep = true;
    bool IsSleeping = false;

    float SleepThreshold = 0.01f;
    float AngularSleepThreshold = 0.01f;
    float SleepTimeThreshold = 0.5f;
    float SleepTimer = 0.0f;

    void SetMass(float mass)
    {
        if (Type == BodyType::Static || mass <= 0.0f)
        {
            Mass = 0.0f;
            InverseMass = 0.0f;
            return;
        }

        Mass = mass;
        InverseMass = 1.0f / mass;
    }

    void SetBodyType(BodyType type)
    {
        Type = type;

        if (Type == BodyType::Static)
        {
            InverseMass = 0.0f;
            LinearVelocity = math::Vec3{};
            AngularVelocity = math::Vec3{};
            Force = math::Vec3{};
            Torque = math::Vec3{};
            IsSleeping = true;
            SleepTimer = 0.0f;
        }
        else
        {
            InverseMass = Mass > 0.0f ? 1.0f / Mass : 0.0f;
            IsSleeping = false;
            SleepTimer = 0.0f;
        }
    }
};

enum class ColliderType
{
    Sphere,
    Box,
    Plane
};

struct ColliderComponent
{
    ColliderType Type = ColliderType::Box;
    math::Vec3 Offset{ 0.0f, 0.0f, 0.0f };
    math::Vec3 HalfExtents{ 0.5f, 0.5f, 0.5f };
    float Radius = 0.5f;
    math::Vec3 PlaneNormal{ 0.0f, 1.0f, 0.0f };
    float PlaneOffset = 0.0f;
    float Restitution = 0.2f;
    float StaticFriction = 0.6f;
    float DynamicFriction = 0.4f;
    bool IsTrigger = false;
};

} // namespace Raven
