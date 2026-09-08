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

// Triangle上でpointに最も近い点を返します。
// Face / Edge / VertexのVoronoi領域を明示的に分けるEricson方式を使い、
// Segment-Triangle最近接計算のendpoint-face候補として再利用します。
inline math::Vec3 ClosestPointOnTriangle(
    const math::Vec3& point,
    const math::Vec3& a,
    const math::Vec3& b,
    const math::Vec3& c)
{
    constexpr float Epsilon = 1.0e-12f;
    const math::Vec3 ab = b - a;
    const math::Vec3 ac = c - a;
    const math::Vec3 triangleNormal = math::Vec3::Cross(ab, ac);

    // 退化Triangleでは面領域を定義できないため、3辺のうち最も近い点へfallbackします。
    if (triangleNormal.LengthSq() <= Epsilon)
    {
        const math::Vec3 pointAB = ClosestPointOnSegment(a, b, point);
        const math::Vec3 pointBC = ClosestPointOnSegment(b, c, point);
        const math::Vec3 pointCA = ClosestPointOnSegment(c, a, point);
        const float distanceAB = (point - pointAB).LengthSq();
        const float distanceBC = (point - pointBC).LengthSq();
        const float distanceCA = (point - pointCA).LengthSq();

        if (distanceAB <= distanceBC && distanceAB <= distanceCA)
        {
            return pointAB;
        }
        if (distanceBC <= distanceCA)
        {
            return pointBC;
        }
        return pointCA;
    }

    const math::Vec3 ap = point - a;
    const float d1 = math::Vec3::Dot(ab, ap);
    const float d2 = math::Vec3::Dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f)
    {
        return a;
    }

    const math::Vec3 bp = point - b;
    const float d3 = math::Vec3::Dot(ab, bp);
    const float d4 = math::Vec3::Dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3)
    {
        return b;
    }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        const float v = d1 / (d1 - d3);
        return a + ab * v;
    }

    const math::Vec3 cp = point - c;
    const float d5 = math::Vec3::Dot(ab, cp);
    const float d6 = math::Vec3::Dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6)
    {
        return c;
    }

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        const float w = d2 / (d2 - d6);
        return a + ac * w;
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        const math::Vec3 bc = c - b;
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + bc * w;
    }

    const float denominator = 1.0f / (va + vb + vc);
    const float v = vb * denominator;
    const float w = vc * denominator;
    return a + ab * v + ac * w;
}

// SegmentとTriangleの最近接点を返します。
// 最短点は「SegmentがTriangle面を貫く」「Segment端点-Triangle面」「Segment-Triangle辺」
// のいずれかに存在するため、その候補をすべて評価して最小距離を選びます。
// これによりCapsule-Terrainを、中心線分とTriangleの距離 <= Radius へ還元できます。
inline void ClosestPointsSegmentTriangle(
    const math::Vec3& segmentA,
    const math::Vec3& segmentB,
    const math::Vec3& triangleA,
    const math::Vec3& triangleB,
    const math::Vec3& triangleC,
    math::Vec3& outSegmentPoint,
    math::Vec3& outTrianglePoint)
{
    constexpr float Epsilon = 1.0e-10f;
    const math::Vec3 segmentDirection = segmentB - segmentA;
    const math::Vec3 triangleNormal = math::Vec3::Cross(
        triangleB - triangleA,
        triangleC - triangleA);
    const float normalLengthSquared = triangleNormal.LengthSq();

    // SegmentがTriangle面を横切り、その交点がTriangle内部なら距離0が厳密な最短です。
    if (normalLengthSquared > Epsilon)
    {
        const float denominator = math::Vec3::Dot(triangleNormal, segmentDirection);
        if (std::abs(denominator) > Epsilon)
        {
            const float t = math::Vec3::Dot(triangleNormal, triangleA - segmentA) / denominator;
            if (t >= 0.0f && t <= 1.0f)
            {
                const math::Vec3 segmentPoint = segmentA + segmentDirection * t;
                const math::Vec3 trianglePoint = ClosestPointOnTriangle(
                    segmentPoint,
                    triangleA,
                    triangleB,
                    triangleC);
                if ((segmentPoint - trianglePoint).LengthSq() <= Epsilon)
                {
                    outSegmentPoint = segmentPoint;
                    outTrianglePoint = trianglePoint;
                    return;
                }
            }
        }
    }

    outSegmentPoint = segmentA;
    outTrianglePoint = ClosestPointOnTriangle(segmentA, triangleA, triangleB, triangleC);
    float bestDistanceSquared = (outSegmentPoint - outTrianglePoint).LengthSq();

    const math::Vec3 endpointBTriangle = ClosestPointOnTriangle(
        segmentB,
        triangleA,
        triangleB,
        triangleC);
    const float endpointBDistanceSquared = (segmentB - endpointBTriangle).LengthSq();
    if (endpointBDistanceSquared < bestDistanceSquared)
    {
        bestDistanceSquared = endpointBDistanceSquared;
        outSegmentPoint = segmentB;
        outTrianglePoint = endpointBTriangle;
    }

    const math::Vec3 edgeStarts[3]{ triangleA, triangleB, triangleC };
    const math::Vec3 edgeEnds[3]{ triangleB, triangleC, triangleA };
    for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex)
    {
        math::Vec3 segmentPoint{};
        math::Vec3 edgePoint{};
        ClosestPointsOnSegments(
            segmentA,
            segmentB,
            edgeStarts[edgeIndex],
            edgeEnds[edgeIndex],
            segmentPoint,
            edgePoint);

        const float distanceSquared = (segmentPoint - edgePoint).LengthSq();
        if (distanceSquared < bestDistanceSquared)
        {
            bestDistanceSquared = distanceSquared;
            outSegmentPoint = segmentPoint;
            outTrianglePoint = edgePoint;
        }
    }
}

} // namespace Raven::ph
