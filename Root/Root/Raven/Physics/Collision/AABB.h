#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{

// ============================================================================
// AABB
// ============================================================================
// Broad Phase専用の軽量なAxis-Aligned Bounding Boxです。
// Min / Maxはワールド座標で保持します。
//
// Dynamic AABB Treeではこの型をLeaf/Branchの両方で使用します。
// そのため単なるOverlap判定だけでなく、Tree更新・挿入コスト計算・Ray Queryに
// 必要な基本演算をここへ集約しています。
struct AABB
{
    math::Vec3 Min{};
    math::Vec3 Max{};

    bool IsValid() const
    {
        return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z;
    }

    math::Vec3 GetCenter() const
    {
        return (Min + Max) * 0.5f;
    }

    math::Vec3 GetExtents() const
    {
        return (Max - Min) * 0.5f;
    }

    bool Overlaps(const AABB& other) const
    {
        // X/Y/Zのどれか1軸でも区間が分離していれば非交差です。
        // 境界が一致する場合は接触候補として残します。
        return !(Max.x < other.Min.x || Min.x > other.Max.x
            || Max.y < other.Min.y || Min.y > other.Max.y
            || Max.z < other.Min.z || Min.z > other.Max.z);
    }

    // ------------------------------------------------------------------------
    // Contains
    // ------------------------------------------------------------------------
    // Dynamic Treeで「現在の実AABBが、以前作ったFat AABBの内側にまだ収まるか」
    // を判定するための重要な演算です。
    // trueならTreeからLeafを抜いて再挿入する必要がありません。
    bool Contains(const AABB& other) const
    {
        return Min.x <= other.Min.x && Min.y <= other.Min.y && Min.z <= other.Min.z
            && Max.x >= other.Max.x && Max.y >= other.Max.y && Max.z >= other.Max.z;
    }

    bool Contains(const math::Vec3& point) const
    {
        return point.x >= Min.x && point.x <= Max.x
            && point.y >= Min.y && point.y <= Max.y
            && point.z >= Min.z && point.z <= Max.z;
    }

    // ------------------------------------------------------------------------
    // SurfaceArea
    // ------------------------------------------------------------------------
    // Dynamic AABB TreeのSAH(Surface Area Heuristic)で使用します。
    // 新しいLeafをどちらの子へ挿入するとTree全体の面積増加が小さいかを
    // 比較するため、体積ではなく表面積を使います。
    float SurfaceArea() const
    {
        const math::Vec3 size = Max - Min;
        if (size.x < 0.0f || size.y < 0.0f || size.z < 0.0f)
        {
            return 0.0f;
        }

        return 2.0f * (size.x * size.y + size.y * size.z + size.z * size.x);
    }

    // 2つのAABBを完全に包む親AABBを生成します。
    static AABB Combine(const AABB& a, const AABB& b)
    {
        return AABB{
            math::Vec3{
                std::min(a.Min.x, b.Min.x),
                std::min(a.Min.y, b.Min.y),
                std::min(a.Min.z, b.Min.z)
            },
            math::Vec3{
                std::max(a.Max.x, b.Max.x),
                std::max(a.Max.y, b.Max.y),
                std::max(a.Max.z, b.Max.z)
            }
        };
    }

    // ------------------------------------------------------------------------
    // Expanded / CreateFat
    // ------------------------------------------------------------------------
    // 実Colliderより少し大きいFat AABBを作ることで、微小移動のたびに
    // Dynamic TreeからLeafをRemove/Insertし直すコストを避けます。
    AABB Expanded(float margin) const
    {
        const float safeMargin = std::max(margin, 0.0f);
        const math::Vec3 extension{ safeMargin, safeMargin, safeMargin };
        return AABB{ Min - extension, Max + extension };
    }

    static AABB CreateFat(
        const AABB& bounds,
        float margin,
        const math::Vec3& displacement = math::Vec3{},
        float displacementMultiplier = 1.0f)
    {
        AABB fat = bounds.Expanded(margin);
        const math::Vec3 prediction = displacement * std::max(displacementMultiplier, 0.0f);

        // 速度方向だけをさらに広げます。
        // これにより次フレーム付近の移動もFat AABB内に残りやすくなります。
        if (prediction.x < 0.0f) fat.Min.x += prediction.x; else fat.Max.x += prediction.x;
        if (prediction.y < 0.0f) fat.Min.y += prediction.y; else fat.Max.y += prediction.y;
        if (prediction.z < 0.0f) fat.Min.z += prediction.z; else fat.Max.z += prediction.z;

        return fat;
    }

    // ------------------------------------------------------------------------
    // RayCast
    // ------------------------------------------------------------------------
    // Slab法でRayとAABBの交差を判定します。
    // directionは正規化されている必要はありません。
    // outFractionは origin + direction * fraction で表される最初の交差位置です。
    bool RayCast(
        const math::Vec3& origin,
        const math::Vec3& direction,
        float maxFraction,
        float& outFraction,
        math::Vec3* outNormal = nullptr) const
    {
        constexpr float parallelEpsilon = 1.0e-8f;

        float tMin = 0.0f;
        float tMax = std::max(maxFraction, 0.0f);
        math::Vec3 hitNormal{};

        for (int axis = 0; axis < 3; ++axis)
        {
            const float o = origin[axis];
            const float d = direction[axis];
            const float minValue = Min[axis];
            const float maxValue = Max[axis];

            if (std::abs(d) <= parallelEpsilon)
            {
                // Rayがこの軸のSlabと平行な場合、originがSlab外なら交差不能です。
                if (o < minValue || o > maxValue)
                {
                    return false;
                }
                continue;
            }

            const float inverseDirection = 1.0f / d;
            float t1 = (minValue - o) * inverseDirection;
            float t2 = (maxValue - o) * inverseDirection;

            float normalSign = -1.0f;
            if (t1 > t2)
            {
                std::swap(t1, t2);
                normalSign = 1.0f;
            }

            if (t1 > tMin)
            {
                tMin = t1;
                hitNormal = math::Vec3{};
                hitNormal[axis] = normalSign;
            }

            tMax = std::min(tMax, t2);
            if (tMin > tMax)
            {
                return false;
            }
        }

        outFraction = tMin;
        if (outNormal != nullptr)
        {
            *outNormal = hitNormal;
        }
        return true;
    }
};

// ============================================================================
// ComputeColliderAABB
// ============================================================================
// ColliderComponent + TransformComponentから、現在フレームのワールドAABBを
// 生成します。Dynamic Tree導入後は、このtight AABBをFat AABBへ拡張してLeafへ
// 格納します。
//
// Sphere / Boxを包むBroad Phase用AABBを生成します。
// Planeは無限形状なので有限AABBを作らずfalseを返します。
bool ComputeColliderAABB(
    const TransformComponent& transform,
    const ColliderComponent& collider,
    AABB& outAABB)
;

} // namespace Raven::ph
