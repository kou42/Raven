// Raven/Scene/Components.h
#pragma once

#include <memory>
#include <string>

#include "Raven/Math/Math.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Math/MathMatrix.h"

namespace Raven
{

class Mesh;
class Material;

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
//   質量は無限大として扱い、InverseMassは0になります。
//
// Kinematic
//   ゲームロジック側が位置または速度を指定するBodyです。
//   現段階ではLinearVelocityによる移動だけを許可し、重力・外力は適用しません。
//   将来は動く床、ドア、エレベーターなどに使用します。
struct RigidBodyComponent
{
    BodyType Type = BodyType::Dynamic;

    float Mass = 1.0f;
    float InverseMass = 1.0f;

    math::Vec3 LinearVelocity{ 0.0f, 0.0f, 0.0f };
    math::Vec3 AngularVelocity{ 0.0f, 0.0f, 0.0f };

    math::Vec3 Force{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Torque{ 0.0f, 0.0f, 0.0f };

    float LinearDamping = 0.01f;
    float AngularDamping = 0.01f;

    bool UseGravity = true;
    bool AllowSleep = true;
    bool IsSleeping = false;

    float SleepThreshold = 0.01f;
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
    // PlaneではTransform::PositionとPlaneOffsetを組み合わせて平面位置を表します。
    math::Vec3 Offset{ 0.0f, 0.0f, 0.0f };

    // Box用パラメータです。第3段階では回転しないAABBとして使用します。
    math::Vec3 HalfExtents{ 0.5f, 0.5f, 0.5f };

    // Sphere用パラメータです。
    float Radius = 0.5f;

    // Plane用パラメータです。
    // PlaneNormalは平面の表側を示す法線です。
    // 設定値が正規化されていなくても、衝突判定側で安全に正規化します。
    math::Vec3 PlaneNormal{ 0.0f, 1.0f, 0.0f };

    // 平面上の基準点を法線方向へ移動させる追加オフセットです。
    // 最終的な平面上の点は
    //
    //     Transform.Position + Offset + normalized(PlaneNormal) * PlaneOffset
    //
    // となります。
    float PlaneOffset = 0.0f;

    float Restitution = 0.2f;
    float StaticFriction = 0.6f;
    float DynamicFriction = 0.4f;

    bool IsTrigger = false;
};

} // namespace Raven
