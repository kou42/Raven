#pragma once

#include <cmath>

#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{

// Broad Phase専用の軽量なAxis-Aligned Bounding Boxです。
// Min / Maxはワールド座標で保持します。
struct AABB
{
    math::Vec3 Min{};
    math::Vec3 Max{};

    bool Overlaps(const AABB& other) const
    {
        // X/Y/Zのどれか1軸でも区間が分離していれば非交差です。
        // 境界が一致する場合は接触候補として残します。
        return !(Max.x < other.Min.x || Min.x > other.Max.x
            || Max.y < other.Min.y || Min.y > other.Max.y
            || Max.z < other.Min.z || Min.z > other.Max.z);
    }
};

// Sphere / Boxを包むBroad Phase用AABBを生成します。
// Planeは無限形状なので有限AABBを作らずfalseを返します。
inline bool ComputeColliderAABB(
    const TransformComponent& transform,
    const ColliderComponent& collider,
    AABB& outAABB)
{
    const math::Vec3 center = transform.Position + collider.Offset;
    math::Vec3 extents{};

    if (collider.Type == ColliderType::Sphere)
    {
        if (collider.Radius <= 0.0f)
        {
            return false;
        }
        extents = math::Vec3{ collider.Radius, collider.Radius, collider.Radius };
    }
    else if (collider.Type == ColliderType::Box)
    {
        // Boxは現在の設計どおり「回転しないAABB」として扱います。
        extents = math::Vec3{
            std::abs(collider.HalfExtents.x),
            std::abs(collider.HalfExtents.y),
            std::abs(collider.HalfExtents.z)
        };
    }
    else
    {
        return false;
    }

    outAABB.Min = center - extents;
    outAABB.Max = center + extents;
    return true;
}

} // namespace Raven::ph
