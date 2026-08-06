// Raven/Scene/Components.h
#pragma once

#include <memory>
#include <string>

#include "Raven/Math/Math.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/Math.h"


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

//質量
//速度
//角速度
//力
//トルク
//物体タイプ
//物理設定
//
//だけを持ち、計算はPhysicsWorldが担当します。

//Dynamic Body
//Physicsが正
//Physicsの計算結果 → Transformへ書き込む
//
//ゲーム側が毎フレームDynamic BodyのTransformを直接変更するのは避けます。
//
//位置を変更したい場合は、
//
//PhysicsWorld::Teleport(entity, position);
//PhysicsWorld::AddForce(entity, force);
//PhysicsWorld::AddImpulse(entity, impulse);
//
//などを使います。
//
//Static Body
//Transformが正
//
//基本的に実行中は動かしません。Transformを変更した場合は、Broad Phaseの再登録が必要になります。
//
//Kinematic Body
//ゲームロジックがTransformまたは目標速度を指定
//Physicsは衝突への影響を計算
//
//動く床、ドア、エレベーターなどに使います。

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
    bool IsSleeping = false;

    void SetMass(float mass)
    {
        Mass = mass;

        if (Type == BodyType::Static || mass <= 0.0f)
            InverseMass = 0.0f;
        else
            InverseMass = 1.0f / mass;
    }
};

enum class ColliderType
{
    Sphere,
    Box,
    Plane
};

// RigidBodyとColliderは分離した方がよいです。
// 分離する理由は、次のようなEntityを表現できるためです。
//
// Transformのみ
//     描画しない空Entityなど
//
// Transform + MeshRenderer
//     見えるが物理には参加しないEntity
//
// Transform + Collider
//     衝突判定だけを持つ静的な壁
//
// Transform + RigidBody + Collider
//     落下・衝突する動的物体
//
// Transform + Collider(IsTrigger)
//     接触イベントだけ発生する領域

struct ColliderComponent
{
    ColliderType Type = ColliderType::Box;

    math::Vec3 Offset{ 0.0f, 0.0f, 0.0f };

    math::Vec3 HalfExtents{ 0.5f, 0.5f, 0.5f };
    float Radius = 0.5f;

    float Restitution = 0.2f;
    float StaticFriction = 0.6f;
    float DynamicFriction = 0.4f;

    bool IsTrigger = false;
};

}
