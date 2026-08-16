#pragma once

#include <algorithm>
#include <cmath>

#include "Raven/Math/MathUtility.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{

// ============================================================================
// Capsule
// ============================================================================
// RavenのCapsuleは「ローカルY軸方向の線分を半径Radiusで膨らませた形状」です。
// SegmentA / SegmentBは半球の中心であり、HalfLengthはCapsule全高の半分ではなく
// 中心線分の半長を表します。
//
// したがって全高は次の式になります。
//
//   FullHeight = 2 * (HalfLength + Radius)
//
// RagdollBodyDefinitionのRadius / HalfLengthと同じ意味に揃えることで、Bridge側で
// Box近似のための変換を挟まず、そのままPhysics Colliderへ渡せます。
struct Capsule
{
    math::Vec3 SegmentA{};
    math::Vec3 SegmentB{};
    float Radius = 0.5f;

    math::Vec3 Center() const
    {
        return (SegmentA + SegmentB) * 0.5f;
    }
};

inline bool ComputeCapsule(
    const TransformComponent& transform,
    const ColliderComponent& collider,
    Capsule& outCapsule)
{
    if (collider.Type != ColliderType::Capsule)
    {
        return false;
    }

    const float radius = std::abs(collider.Radius);
    const float halfLength = std::abs(collider.HalfLength);
    if (radius <= 0.0f)
    {
        return false;
    }

    // Boxと同じTransform回転規約 Rx * Ry * Rz を利用します。
    // OffsetもColliderローカル座標として回転させるため、Ragdoll Bone姿勢に追従します。
    math::Mat4 rotation = math::Mat4::Identity();
    rotation = math::Rotate(rotation, transform.Rotation.x, math::Vec3{ 1.0f, 0.0f, 0.0f });
    rotation = math::Rotate(rotation, transform.Rotation.y, math::Vec3{ 0.0f, 1.0f, 0.0f });
    rotation = math::Rotate(rotation, transform.Rotation.z, math::Vec3{ 0.0f, 0.0f, 1.0f });

    const math::Vec3 axisX = math::Vec3{ rotation[0][0], rotation[1][0], rotation[2][0] }.Normalized();
    const math::Vec3 axisY = math::Vec3{ rotation[0][1], rotation[1][1], rotation[2][1] }.Normalized();
    const math::Vec3 axisZ = math::Vec3{ rotation[0][2], rotation[1][2], rotation[2][2] }.Normalized();

    const math::Vec3 center = transform.Position
        + axisX * collider.Offset.x
        + axisY * collider.Offset.y
        + axisZ * collider.Offset.z;

    outCapsule.SegmentA = center - axisY * halfLength;
    outCapsule.SegmentB = center + axisY * halfLength;
    outCapsule.Radius = radius;
    return true;
}

// 線分上でpointに最も近い点を返します。
inline math::Vec3 ClosestPointOnSegment(
    const math::Vec3& a,
    const math::Vec3& b,
    const math::Vec3& point,
    float* outT = nullptr)
{
    const math::Vec3 ab = b - a;
    const float lengthSquared = ab.LengthSq();
    float t = 0.0f;
    if (lengthSquared > 1.0e-12f)
    {
        t = std::clamp(math::Vec3::Dot(point - a, ab) / lengthSquared, 0.0f, 1.0f);
    }

    if (outT != nullptr)
    {
        *outT = t;
    }
    return a + ab * t;
}

// 2本の線分間の最近接点を求めます。
// Capsule-Capsuleは「中心線分同士の距離 <= 半径和」へ還元できるため、
// Narrow Phaseの中心となるユーティリティです。
inline void ClosestPointsOnSegments(
    const math::Vec3& p1,
    const math::Vec3& q1,
    const math::Vec3& p2,
    const math::Vec3& q2,
    math::Vec3& outPoint1,
    math::Vec3& outPoint2)
{
    constexpr float Epsilon = 1.0e-12f;
    const math::Vec3 d1 = q1 - p1;
    const math::Vec3 d2 = q2 - p2;
    const math::Vec3 r = p1 - p2;
    const float a = math::Vec3::Dot(d1, d1);
    const float e = math::Vec3::Dot(d2, d2);
    const float f = math::Vec3::Dot(d2, r);

    float s = 0.0f;
    float t = 0.0f;

    if (a <= Epsilon && e <= Epsilon)
    {
        outPoint1 = p1;
        outPoint2 = p2;
        return;
    }

    if (a <= Epsilon)
    {
        t = std::clamp(f / e, 0.0f, 1.0f);
    }
    else
    {
        const float c = math::Vec3::Dot(d1, r);
        if (e <= Epsilon)
        {
            s = std::clamp(-c / a, 0.0f, 1.0f);
        }
        else
        {
            const float b = math::Vec3::Dot(d1, d2);
            const float denominator = a * e - b * b;
            if (std::abs(denominator) > Epsilon)
            {
                s = std::clamp((b * f - c * e) / denominator, 0.0f, 1.0f);
            }

            const float tNumerator = b * s + f;
            if (tNumerator < 0.0f)
            {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            }
            else if (tNumerator > e)
            {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
            else
            {
                t = tNumerator / e;
            }
        }
    }

    outPoint1 = p1 + d1 * s;
    outPoint2 = p2 + d2 * t;
}

} // namespace Raven::ph
