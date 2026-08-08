#pragma once

#include <algorithm>
#include <cmath>

#include "Raven/Math/MathUtility.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{

// ============================================================================
// OBB
// ============================================================================
// Narrow Phaseで使用するOriented Bounding Boxです。
//
// Center
//   ワールド空間中心。
// Axis[0..2]
//   BoxのローカルX/Y/Z軸をワールドへ回転した正規直交基底。
// HalfExtents
//   各Axis方向の半サイズ。
//
// RavenではCollider寸法とTransform::Scaleを従来から分離しているため、
// OBB導入でもHalfExtentsへScaleは掛けません。これにより既存Sceneの衝突サイズを
// 変更せず、Box Rotationだけを段階的に有効化できます。
struct OBB
{
    math::Vec3 Center{};
    math::Vec3 HalfExtents{ 0.5f, 0.5f, 0.5f };
    math::Vec3 Axis[3]{
        math::Vec3{ 1.0f, 0.0f, 0.0f },
        math::Vec3{ 0.0f, 1.0f, 0.0f },
        math::Vec3{ 0.0f, 0.0f, 1.0f }
    };

    math::Vec3 ToWorldPoint(const math::Vec3& localPoint) const
    {
        return Center
            + Axis[0] * localPoint.x
            + Axis[1] * localPoint.y
            + Axis[2] * localPoint.z;
    }

    math::Vec3 ToLocalPoint(const math::Vec3& worldPoint) const
    {
        const math::Vec3 delta = worldPoint - Center;
        return {
            math::Vec3::Dot(delta, Axis[0]),
            math::Vec3::Dot(delta, Axis[1]),
            math::Vec3::Dot(delta, Axis[2])
        };
    }

    math::Vec3 Support(const math::Vec3& direction) const
    {
        math::Vec3 point = Center;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float sign = math::Vec3::Dot(direction, Axis[axis]) >= 0.0f ? 1.0f : -1.0f;
            point += Axis[axis] * (HalfExtents[axis] * sign);
        }
        return point;
    }
};

// ============================================================================
// ComputeBoxOBB
// ============================================================================
// TransformComponent + Box ColliderからワールドOBBを構築します。
// Transform::GetTransform()と同じ X -> Y -> Z のEuler回転順を使用します。
//
// Collider::Offsetはローカル空間値なので、Boxの回転基底でワールドへ変換してから
// Transform::Positionへ加えます。これにより回転BoxのCollider中心も見た目と同じ
// 向きで移動します。
inline bool ComputeBoxOBB(
    const TransformComponent& transform,
    const ColliderComponent& collider,
    OBB& outOBB)
{
    if (collider.Type != ColliderType::Box)
    {
        return false;
    }

    const math::Vec3 halfExtents{
        std::abs(collider.HalfExtents.x),
        std::abs(collider.HalfExtents.y),
        std::abs(collider.HalfExtents.z)
    };

    // 退化したBoxはSATの投影半径や接触面生成を不安定にするため除外します。
    if (halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f)
    {
        return false;
    }

    math::Mat4 rotation = math::Mat4::Identity();
    rotation = math::Rotate(rotation, transform.Rotation.x, math::Vec3{ 1.0f, 0.0f, 0.0f });
    rotation = math::Rotate(rotation, transform.Rotation.y, math::Vec3{ 0.0f, 1.0f, 0.0f });
    rotation = math::Rotate(rotation, transform.Rotation.z, math::Vec3{ 0.0f, 0.0f, 1.0f });

    // column-vector方式なので、回転行列の各columnがローカル基底のワールド方向です。
    outOBB.Axis[0] = math::Vec3{ rotation[0][0], rotation[1][0], rotation[2][0] }.Normalized();
    outOBB.Axis[1] = math::Vec3{ rotation[0][1], rotation[1][1], rotation[2][1] }.Normalized();
    outOBB.Axis[2] = math::Vec3{ rotation[0][2], rotation[1][2], rotation[2][2] }.Normalized();
    outOBB.HalfExtents = halfExtents;

    outOBB.Center = transform.Position
        + outOBB.Axis[0] * collider.Offset.x
        + outOBB.Axis[1] * collider.Offset.y
        + outOBB.Axis[2] * collider.Offset.z;

    return true;
}

} // namespace Raven::ph
