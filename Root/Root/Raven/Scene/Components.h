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
class Animator;

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
// Scene側はWave / Skeletal / Morph / SoftBodyなどの具体型を知りません。
// 具体的な変形アルゴリズムとMeshの組はMeshDeformationInstanceへ隠し、
// MeshDeformationSystemがInstance::Update()を呼ぶ構成にします。
//
// shared_ptrを使う理由:
// Instance内部ではDeformerをunique ownershipします。一方ComponentはStorage内で移動・再配置
// され得るため、Component自体は軽量な共有Handleとして所有権の境界を分離します。
struct MeshDeformationComponent
{
    std::shared_ptr<MeshDeformationInstance> Instance = nullptr;
    bool Enabled = true;

    bool IsValid() const
    {
        return Instance != nullptr;
    }
};

// ============================================================================
// AnimatorComponent
// ============================================================================
// EntityがAnimation再生状態を持つことだけをScene/ECSへ公開するComponentです。
// 実際のCurrentTime / Loop / Speed / Pose評価はAnimatorが担当し、
// AnimationSystemがAnimator::Update()の結果をTransformComponentへ反映します。
//
// MeshDeformationComponentと同じくComponent自体は軽量なHandleに留めます。
// 同じAnimationClipを複数Animatorが共有しても、再生時刻はAnimatorごとに独立します。
struct AnimatorComponent
{
    std::shared_ptr<Animator> Instance = nullptr;
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

// ============================================================================
// RigidBodyComponent
// ============================================================================
// 剛体の「状態データ」だけを保持するComponentです。
// 実際の計算はPhysicsWorldが担当します。
//
// BodyTypeの役割
// ---------------------------------------------------------------------------
// Dynamic
//   重力・外力・衝突によって動く通常の物理Bodyです。
//   PhysicsWorldの計算結果をTransformへ書き込みます。
//
// Static
//   床や壁など、物理演算中に動かないBodyです。
//   質量・慣性は無限大として扱います。
//
// Kinematic
//   ゲームロジック側が位置または速度を指定するBodyです。
//   外力・衝突Impulseでは動かしません。
struct RigidBodyComponent
{
    BodyType Type = BodyType::Dynamic;

    float Mass = 1.0f;
    float InverseMass = 1.0f;

    math::Vec3 LinearVelocity{ 0.0f, 0.0f, 0.0f };
    math::Vec3 AngularVelocity{ 0.0f, 0.0f, 0.0f };

    math::Vec3 Force{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Torque{ 0.0f, 0.0f, 0.0f };

    // ========================================================================
    // Physics Orientation
    // ========================================================================
    // 物理内部ではEuler角を積分せずQuaternionを正規姿勢として扱います。
    // Euler角へ直接 AngularVelocity * dt を足す方法では、複数軸回転時に回転順依存と
    // gimbal lockの影響が積み重なるためです。
    //
    // Transform::RotationはRenderer / Scene互換用のミラーとして残し、Physics Step中に
    // Orientationから同期します。初回StepだけTransform::Rotationから初期化します。
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

// ============================================================================
// ColliderComponent
// ============================================================================
// RigidBodyとは分離して保持します。
// これにより、見えるだけのEntity、衝突だけを持つ壁、Trigger領域などを
// 同じScene/ECS上で表現できます。
struct ColliderComponent
{
    ColliderType Type = ColliderType::Box;

    // Sphere / Boxの中心をTransform::Positionからずらすローカルオフセットです。
    // BoxではTransformの回転に追従します。
    math::Vec3 Offset{ 0.0f, 0.0f, 0.0f };

    // Box用パラメータです。現在はTransform::Rotationを反映したOBBの半サイズです。
    // 既存Scene互換性のためTransform::Scaleとは分離しています。
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
