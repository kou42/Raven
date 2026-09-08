#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Collision/AABB.h"

namespace Raven
{
struct ColliderComponent;
struct TransformComponent;
}

namespace Raven::ph
{
class StaticMeshTriangleBVH;

// World RayをStaticMesh Geometryのローカル空間へ変換し、BVH候補Triangleを返します。
// Entity Transformが非特異なTRSである限りRay parameter tは変換前後で一致するため、
// maxFractionをそのままBVH Queryへ渡せます。
bool QueryStaticMeshBVHByWorldRay(
    const math::Vec3& worldOrigin,
    const math::Vec3& worldDirection,
    float maxFraction,
    const TransformComponent& staticMeshTransform,
    const ColliderComponent& staticMeshCollider,
    std::shared_ptr<const StaticMeshTriangleBVH>& outBVH,
    std::vector<uint32_t>& outTriangleIndices);

// World AABBの8 cornerをMesh Localへ変換し、その点群を包むLocal AABBでBVHをQueryします。
// 回転・非一様Scale後のWorld AABBを逆変換すると一般にはOBBになるため、そのLocal AABBを
// さらに包むことで候補が増える可能性は許容しつつ、Triangle取りこぼしを防ぎます。
bool QueryStaticMeshBVHByWorldAABB(
    const AABB& worldBounds,
    const TransformComponent& staticMeshTransform,
    const ColliderComponent& staticMeshCollider,
    std::shared_ptr<const StaticMeshTriangleBVH>& outBVH,
    std::vector<uint32_t>& outTriangleIndices);

} // namespace Raven::ph
