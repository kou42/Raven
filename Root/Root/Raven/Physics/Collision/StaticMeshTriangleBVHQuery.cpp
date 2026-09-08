#include "Raven/Physics/Collision/StaticMeshTriangleBVHQuery.h"

#include <algorithm>
#include <cmath>

#include "Raven/Physics/Collision/StaticMeshTriangleBVH.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{
namespace
{

bool BuildInverseStaticMeshTransform(
    const TransformComponent& transform,
    math::Mat4& outInverseTransform)
{
    constexpr float scaleEpsilon = 1.0e-8f;
    if (std::abs(transform.Scale.x) <= scaleEpsilon
        || std::abs(transform.Scale.y) <= scaleEpsilon
        || std::abs(transform.Scale.z) <= scaleEpsilon)
    {
        return false;
    }

    // TransformComponent::GetTransform()は
    //   T * Rx * Ry * Rz * S
    // の順でLocal Pointへ作用します。したがって逆変換は右から逆順に
    //   S^-1 * Rz^-1 * Ry^-1 * Rx^-1 * T^-1
    // です。一般4x4 inverseを導入せず、既存Transform規約を明示したまま構築します。
    const math::Mat4 inverseScale = math::Mat4::Scaling(math::Vec3{
        1.0f / transform.Scale.x,
        1.0f / transform.Scale.y,
        1.0f / transform.Scale.z
    });
    const math::Mat4 inverseRotationZ = math::Mat4::RotationZ(-transform.Rotation.z);
    const math::Mat4 inverseRotationY = math::Mat4::RotationY(-transform.Rotation.y);
    const math::Mat4 inverseRotationX = math::Mat4::RotationX(-transform.Rotation.x);
    const math::Mat4 inverseTranslation = math::Mat4::Translation(-transform.Position);

    outInverseTransform = inverseScale
        * inverseRotationZ
        * inverseRotationY
        * inverseRotationX
        * inverseTranslation;
    return true;
}

math::Vec3 TransformPointToStaticMeshLocal(
    const math::Mat4& inverseTransform,
    const math::Vec3& colliderOffset,
    const math::Vec3& worldPoint)
{
    const math::Vec4 localWithOffset = inverseTransform * math::Vec4{ worldPoint, 1.0f };
    return math::Vec3{
        localWithOffset.x - colliderOffset.x,
        localWithOffset.y - colliderOffset.y,
        localWithOffset.z - colliderOffset.z
    };
}

math::Vec3 TransformDirectionToStaticMeshLocal(
    const math::Mat4& inverseTransform,
    const math::Vec3& worldDirection)
{
    const math::Vec4 localDirection = inverseTransform * math::Vec4{ worldDirection, 0.0f };
    return math::Vec3{ localDirection.x, localDirection.y, localDirection.z };
}

bool ResolveStaticMeshBVH(
    const ColliderComponent& collider,
    std::shared_ptr<const StaticMeshTriangleBVH>& outBVH)
{
    if (collider.Type != ColliderType::StaticMesh
        || collider.StaticMeshGeometry == nullptr)
    {
        return false;
    }

    outBVH = StaticMeshTriangleBVH::GetOrBuildCached(collider.StaticMeshGeometry);
    return outBVH != nullptr && outBVH->IsEmpty() == false;
}

} // namespace

bool QueryStaticMeshBVHByWorldRay(
    const math::Vec3& worldOrigin,
    const math::Vec3& worldDirection,
    float maxFraction,
    const TransformComponent& staticMeshTransform,
    const ColliderComponent& staticMeshCollider,
    std::shared_ptr<const StaticMeshTriangleBVH>& outBVH,
    std::vector<uint32_t>& outTriangleIndices)
{
    outBVH.reset();
    outTriangleIndices.clear();

    if (maxFraction < 0.0f
        || worldDirection.LengthSq() <= 1.0e-12f
        || ResolveStaticMeshBVH(staticMeshCollider, outBVH) == false)
    {
        return false;
    }

    math::Mat4 inverseTransform{};
    if (BuildInverseStaticMeshTransform(staticMeshTransform, inverseTransform) == false)
    {
        outBVH.reset();
        return false;
    }

    const math::Vec3 localOrigin = TransformPointToStaticMeshLocal(
        inverseTransform,
        staticMeshCollider.Offset,
        worldOrigin);
    const math::Vec3 localDirection = TransformDirectionToStaticMeshLocal(
        inverseTransform,
        worldDirection);
    if (localDirection.LengthSq() <= 1.0e-12f)
    {
        outBVH.reset();
        return false;
    }

    outBVH->QueryRay(
        localOrigin,
        localDirection,
        maxFraction,
        outTriangleIndices);
    return true;
}

bool QueryStaticMeshBVHByWorldAABB(
    const AABB& worldBounds,
    const TransformComponent& staticMeshTransform,
    const ColliderComponent& staticMeshCollider,
    std::shared_ptr<const StaticMeshTriangleBVH>& outBVH,
    std::vector<uint32_t>& outTriangleIndices)
{
    outBVH.reset();
    outTriangleIndices.clear();

    if (worldBounds.IsValid() == false
        || ResolveStaticMeshBVH(staticMeshCollider, outBVH) == false)
    {
        return false;
    }

    math::Mat4 inverseTransform{};
    if (BuildInverseStaticMeshTransform(staticMeshTransform, inverseTransform) == false)
    {
        outBVH.reset();
        return false;
    }

    math::Vec3 localMinimum{};
    math::Vec3 localMaximum{};
    bool initialized = false;

    for (int x = 0; x < 2; ++x)
    {
        for (int y = 0; y < 2; ++y)
        {
            for (int z = 0; z < 2; ++z)
            {
                const math::Vec3 worldCorner{
                    x == 0 ? worldBounds.Min.x : worldBounds.Max.x,
                    y == 0 ? worldBounds.Min.y : worldBounds.Max.y,
                    z == 0 ? worldBounds.Min.z : worldBounds.Max.z
                };
                const math::Vec3 localCorner = TransformPointToStaticMeshLocal(
                    inverseTransform,
                    staticMeshCollider.Offset,
                    worldCorner);

                if (initialized == false)
                {
                    localMinimum = localCorner;
                    localMaximum = localCorner;
                    initialized = true;
                    continue;
                }

                localMinimum.x = std::min(localMinimum.x, localCorner.x);
                localMinimum.y = std::min(localMinimum.y, localCorner.y);
                localMinimum.z = std::min(localMinimum.z, localCorner.z);
                localMaximum.x = std::max(localMaximum.x, localCorner.x);
                localMaximum.y = std::max(localMaximum.y, localCorner.y);
                localMaximum.z = std::max(localMaximum.z, localCorner.z);
            }
        }
    }

    if (initialized == false)
    {
        outBVH.reset();
        return false;
    }

    outBVH->QueryAABB(localMinimum, localMaximum, outTriangleIndices);
    return true;
}

} // namespace Raven::ph
