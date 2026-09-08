#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/StaticMeshTriangleBVH.h"

namespace Raven
{
struct ColliderComponent;
struct TransformComponent;
}

namespace Raven::ph
{

// World RayをStaticMesh Geometryのローカル空間へ変換し、BVH候補Triangleを返します。
// Entity Transformが非特異なTRSである限りRay parameter tは変換前後で一致します。
bool QueryStaticMeshBVHByWorldRay(
    const math::Vec3& worldOrigin,
    const math::Vec3& worldDirection,
    float maxFraction,
    const TransformComponent& staticMeshTransform,
    const ColliderComponent& staticMeshCollider,
    std::shared_ptr<const StaticMeshTriangleBVH>& outBVH,
    std::vector<uint32_t>& outTriangleIndices,
    StaticMeshTriangleBVH::QueryStatistics* statistics = nullptr);

// World AABBの8 cornerをMesh Localへ変換し、その点群を包むLocal AABBでBVHをQueryします。
// 回転・非一様Scaleでは保守的Local AABBとなるため、候補増加を許容して取りこぼしを防ぎます。
bool QueryStaticMeshBVHByWorldAABB(
    const AABB& worldBounds,
    const TransformComponent& staticMeshTransform,
    const ColliderComponent& staticMeshCollider,
    std::shared_ptr<const StaticMeshTriangleBVH>& outBVH,
    std::vector<uint32_t>& outTriangleIndices,
    StaticMeshTriangleBVH::QueryStatistics* statistics = nullptr);

} // namespace Raven::ph
