// Raven/Physics/PhysicsWorldCapsuleCast.cpp
#include "Raven/Physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "Raven/Physics/Collision/Capsule.h"
#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{

bool PassesCapsuleCastFilter(
    Scene& scene,
    Entity entity,
    const ColliderComponent& collider,
    const PhysicsCapsuleCastSettings& settings)
{
    if (collider.IsTrigger == true && settings.IncludeTriggers == false)
    {
        return false;
    }

    if (collider.Type == ColliderType::Plane && settings.IncludePlanes == false)
    {
        return false;
    }

    const RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody == nullptr)
    {
        return settings.IncludeStatic;
    }

    if (rigidBody->Type == BodyType::Static)
    {
        return settings.IncludeStatic;
    }
    if (rigidBody->Type == BodyType::Kinematic)
    {
        return settings.IncludeKinematic;
    }
    if (rigidBody->Type == BodyType::Dynamic)
    {
        return settings.IncludeDynamic;
    }

    return false;
}

TransformComponent BuildCastTransform(
    const math::Vec3& footPosition,
    float radius,
    float halfLength)
{
    TransformComponent transform{};

    // Character ControllerのPositionは足元です。
    // Capsuleの中心線分中央は足元から Radius + HalfLength 上にあるため、
    // Physics Collider側の「Transform位置=Capsule中心」契約へここで変換します。
    transform.Position = footPosition + math::Vec3{ 0.0f, radius + halfLength, 0.0f };
    transform.Rotation = math::Vec3{};
    return transform;
}

ColliderComponent BuildCastCollider(const PhysicsCapsuleCastSettings& settings)
{
    ColliderComponent collider{};
    collider.Type = ColliderType::Capsule;
    collider.Offset = math::Vec3{};

    // SkinWidth分だけ半径を膨らませてCastします。
    // これによりTOI位置で実Capsuleと障害物の間に僅かな余裕を残し、次Frameに
    // 初期Overlapへ入ることを抑えます。
    collider.Radius = settings.Radius + settings.SkinWidth;
    collider.HalfLength = settings.HalfLength;
    collider.IsTrigger = false;
    return collider;
}

math::Vec3 TransformStaticMeshPoint(
    const math::Mat4& worldTransform,
    const math::Vec3& colliderOffset,
    const math::Vec3& localPoint)
{
    const math::Vec4 worldPoint = worldTransform * math::Vec4{
        localPoint.x + colliderOffset.x,
        localPoint.y + colliderOffset.y,
        localPoint.z + colliderOffset.z,
        1.0f
    };
    return math::Vec3{ worldPoint.x, worldPoint.y, worldPoint.z };
}

bool GenerateCapsuleStaticMeshOverlap(
    Entity targetEntity,
    const TransformComponent& castTransform,
    const ColliderComponent& castCollider,
    const TransformComponent& targetTransform,
    const ColliderComponent& targetCollider,
    ContactManifold& outManifold,
    math::Vec3& outObstacleNormal)
{
    if (targetCollider.Type != ColliderType::StaticMesh
        || targetCollider.StaticMeshGeometry == nullptr)
    {
        return false;
    }

    Capsule capsule{};
    if (ComputeCapsule(castTransform, castCollider, capsule) == false)
    {
        return false;
    }

    const auto& vertices = targetCollider.StaticMeshGeometry->GetVertices();
    const auto& indices = targetCollider.StaticMeshGeometry->GetIndices();
    if (vertices.size() < 3u)
    {
        return false;
    }

    // ========================================================================
    // Capsule - Static Mesh overlap
    // ========================================================================
    // Capsuleを中心線分+Radiusへ還元し、各TriangleとのSegment-Triangle最近接距離を
    // 評価します。複数Triangleへ同時に触れた場合はPenetrationが最大のTriangleを採用し、
    // CharacterのSlide方向を最も強い制約面へ合わせます。
    //
    // 現段階はTerrain Collisionの正しさを優先して全Triangleを走査します。
    // API境界はこの関数へ閉じているため、将来BVHを追加してもCapsuleCast側のTOI探索や
    // Character Controllerの呼び出し契約を変更せず候補Triangleだけへ絞り込めます。
    const math::Mat4 worldTransform = targetTransform.GetTransform();
    const float radiusSquared = capsule.Radius * capsule.Radius;
    bool found = false;
    float bestPenetration = -std::numeric_limits<float>::max();
    math::Vec3 bestNormal{};
    math::Vec3 bestPosition{};

    const auto testTriangle =
        [&](std::size_t indexA, std::size_t indexB, std::size_t indexC)
        {
            if (indexA >= vertices.size()
                || indexB >= vertices.size()
                || indexC >= vertices.size())
            {
                // 壊れたTriangleだけを除外し、正常なTerrain Triangleの判定は継続します。
                return;
            }

            const math::Vec3 triangleA = TransformStaticMeshPoint(
                worldTransform,
                targetCollider.Offset,
                vertices[indexA].Position);
            const math::Vec3 triangleB = TransformStaticMeshPoint(
                worldTransform,
                targetCollider.Offset,
                vertices[indexB].Position);
            const math::Vec3 triangleC = TransformStaticMeshPoint(
                worldTransform,
                targetCollider.Offset,
                vertices[indexC].Position);

            const math::Vec3 rawTriangleNormal = math::Vec3::Cross(
                triangleB - triangleA,
                triangleC - triangleA);
            const float triangleNormalLengthSquared = rawTriangleNormal.LengthSq();
            if (triangleNormalLengthSquared <= 1.0e-12f)
            {
                return;
            }

            math::Vec3 capsulePoint{};
            math::Vec3 trianglePoint{};
            ClosestPointsSegmentTriangle(
                capsule.SegmentA,
                capsule.SegmentB,
                triangleA,
                triangleB,
                triangleC,
                capsulePoint,
                trianglePoint);

            const math::Vec3 delta = trianglePoint - capsulePoint;
            const float distanceSquared = delta.LengthSq();
            if (distanceSquared > radiusSquared)
            {
                return;
            }

            float distance = 0.0f;
            math::Vec3 capsuleToMeshNormal{};
            if (distanceSquared > 1.0e-12f)
            {
                distance = std::sqrt(distanceSquared);
                capsuleToMeshNormal = delta / distance;
            }
            else
            {
                // 中心線分がTriangle面を横切る深いOverlapでは最近接差分から法線を作れません。
                // Triangle windingを基準にしつつ、TriangleからCapsule中心へ向く側を障害物法線に
                // 揃えることで、A(Capsule)->B(Mesh)法線はその反対向きとして一意にします。
                math::Vec3 obstacleNormal = rawTriangleNormal / std::sqrt(triangleNormalLengthSquared);
                if (math::Vec3::Dot(obstacleNormal, capsule.Center() - trianglePoint) < 0.0f)
                {
                    obstacleNormal = -obstacleNormal;
                }
                capsuleToMeshNormal = -obstacleNormal;
            }

            const float penetration = capsule.Radius - distance;
            if (found == true && penetration <= bestPenetration)
            {
                return;
            }

            const math::Vec3 capsuleSurface = capsulePoint + capsuleToMeshNormal * capsule.Radius;
            found = true;
            bestPenetration = penetration;
            bestNormal = capsuleToMeshNormal;
            bestPosition = (capsuleSurface + trianglePoint) * 0.5f;
        };

    if (indices.empty() == false)
    {
        for (std::size_t index = 0u; index + 2u < indices.size(); index += 3u)
        {
            testTriangle(indices[index], indices[index + 1u], indices[index + 2u]);
        }
    }
    else
    {
        for (std::size_t index = 0u; index + 2u < vertices.size(); index += 3u)
        {
            testTriangle(index, index + 1u, index + 2u);
        }
    }

    if (found == false)
    {
        return false;
    }

    ContactPoint point{};
    point.Position = bestPosition;
    point.Penetration = bestPenetration;

    outManifold = ContactManifold{};
    outManifold.A = Entity{};
    outManifold.B = targetEntity;
    outManifold.Normal = bestNormal;
    outManifold.IsTrigger = targetCollider.IsTrigger;
    outManifold.AddPoint(point);

    // Manifold NormalはA(Cast Capsule)->B(StaticMesh)なので、Character側が必要とする
    // 「障害物表面からCapsuleへ向く法線」へ反転します。
    outObstacleNormal = -bestNormal;
    return true;
}

bool GenerateCastOverlap(
    Entity targetEntity,
    const TransformComponent& castTransform,
    const ColliderComponent& castCollider,
    const TransformComponent& targetTransform,
    const ColliderComponent& targetCollider,
    ContactManifold& outManifold,
    math::Vec3& outObstacleNormal)
{
    const Entity castEntity{};

    if (targetCollider.Type == ColliderType::Sphere)
    {
        // Sphere(A) -> Capsule(B)で生成したManifold Normalは障害物SphereからCast Capsule方向です。
        if (GenerateSphereCapsuleManifold(
                targetEntity,
                targetTransform,
                targetCollider,
                castEntity,
                castTransform,
                castCollider,
                outManifold) == false)
        {
            return false;
        }

        outObstacleNormal = outManifold.Normal;
        return true;
    }

    if (targetCollider.Type == ColliderType::Box)
    {
        if (GenerateCapsuleBoxManifold(
                castEntity,
                castTransform,
                castCollider,
                targetEntity,
                targetTransform,
                targetCollider,
                outManifold) == false)
        {
            return false;
        }

        // Capsule(A) -> Box(B)法線なので、障害物表面からCapsuleへ向く法線へ反転します。
        outObstacleNormal = -outManifold.Normal;
        return true;
    }

    if (targetCollider.Type == ColliderType::Capsule)
    {
        if (GenerateCapsuleCapsuleManifold(
                castEntity,
                castTransform,
                castCollider,
                targetEntity,
                targetTransform,
                targetCollider,
                outManifold) == false)
        {
            return false;
        }

        outObstacleNormal = -outManifold.Normal;
        return true;
    }

    if (targetCollider.Type == ColliderType::StaticMesh)
    {
        return GenerateCapsuleStaticMeshOverlap(
            targetEntity,
            castTransform,
            castCollider,
            targetTransform,
            targetCollider,
            outManifold,
            outObstacleNormal);
    }

    if (targetCollider.Type == ColliderType::Plane)
    {
        if (GenerateCapsulePlaneManifold(
                castEntity,
                castTransform,
                castCollider,
                targetEntity,
                targetTransform,
                targetCollider,
                outManifold) == false)
        {
            return false;
        }

        outObstacleNormal = -outManifold.Normal;
        return true;
    }

    return false;
}

bool FindBlockingOverlap(
    Scene& scene,
    const math::Vec3& footPosition,
    const math::Vec3& displacement,
    const PhysicsCapsuleCastSettings& settings,
    Entity& outEntity,
    math::Vec3& outPoint,
    math::Vec3& outNormal)
{
    const TransformComponent castTransform = BuildCastTransform(
        footPosition,
        settings.Radius + settings.SkinWidth,
        settings.HalfLength);
    const ColliderComponent castCollider = BuildCastCollider(settings);

    bool foundBlockingOverlap = false;
    float bestIntoSurface = 0.0f;

    for (auto [entity, targetTransform, targetCollider]
        : scene.View<TransformComponent, ColliderComponent>())
    {
        if (PassesCapsuleCastFilter(scene, entity, targetCollider, settings) == false)
        {
            continue;
        }

        ContactManifold manifold{};
        math::Vec3 obstacleNormal{};
        if (GenerateCastOverlap(
                entity,
                castTransform,
                castCollider,
                targetTransform,
                targetCollider,
                manifold,
                obstacleNormal) == false)
        {
            continue;
        }

        const float normalLengthSquared = obstacleNormal.LengthSq();
        if (normalLengthSquared <= 1.0e-12f)
        {
            continue;
        }
        obstacleNormal /= std::sqrt(normalLengthSquared);

        // Capsuleが接触していても、その面へ進んでいない場合はBlocking Hitではありません。
        // 例: 床の上を水平移動するとき floorNormal=(0,1,0) とのDotは0なので、床接触が
        // 水平移動を止めることはありません。
        const float intoSurface = math::Vec3::Dot(displacement, obstacleNormal);
        if (intoSurface >= -1.0e-6f)
        {
            continue;
        }

        if (foundBlockingOverlap == false || intoSurface < bestIntoSurface)
        {
            foundBlockingOverlap = true;
            bestIntoSurface = intoSurface;
            outEntity = entity;
            outNormal = obstacleNormal;
            outPoint = manifold.PointCount > 0u
                ? manifold.Points[0].Position
                : castTransform.Position;
        }
    }

    return foundBlockingOverlap;
}

} // namespace

bool PhysicsWorld::CapsuleCast(
    Scene& scene,
    const math::Vec3& startFootPosition,
    const math::Vec3& displacement,
    const PhysicsCapsuleCastSettings& settings,
    PhysicsCapsuleCastHit& outHit)
{
    if (std::isfinite(settings.Radius) == false
        || std::isfinite(settings.HalfLength) == false
        || std::isfinite(settings.SkinWidth) == false
        || settings.Radius <= 0.0f
        || settings.HalfLength < 0.0f
        || settings.SkinWidth < 0.0f
        || settings.MaxSubsteps == 0u
        || settings.BinarySearchIterations == 0u)
    {
        return false;
    }

    const float displacementLengthSquared = displacement.LengthSq();
    if (displacementLengthSquared <= 1.0e-12f)
    {
        return false;
    }

    const float displacementLength = std::sqrt(displacementLengthSquared);
    const float probeStepLength = std::max(settings.Radius * 0.5f, 0.02f);
    const uint32_t requestedSteps = static_cast<uint32_t>(
        std::ceil(displacementLength / probeStepLength));
    const uint32_t stepCount = std::clamp(
        requestedSteps,
        1u,
        settings.MaxSubsteps);

    Entity blockingEntity{};
    math::Vec3 blockingPoint{};
    math::Vec3 blockingNormal{};

    // 初期Overlapも確認します。すでに壁へ僅かに食い込んだ状態ならfraction=0を返し、
    // Character側のSlide処理がさらに壁内部へ進むことを防ぎます。
    if (FindBlockingOverlap(
            scene,
            startFootPosition,
            displacement,
            settings,
            blockingEntity,
            blockingPoint,
            blockingNormal) == true)
    {
        outHit = PhysicsCapsuleCastHit{};
        outHit.HitEntity = blockingEntity;
        outHit.Position = startFootPosition;
        outHit.Point = blockingPoint;
        outHit.Normal = blockingNormal;
        outHit.Fraction = 0.0f;
        return true;
    }

    float previousFraction = 0.0f;
    for (uint32_t step = 1u; step <= stepCount; ++step)
    {
        const float currentFraction = static_cast<float>(step) / static_cast<float>(stepCount);
        const math::Vec3 currentFootPosition = startFootPosition + displacement * currentFraction;

        if (FindBlockingOverlap(
                scene,
                currentFootPosition,
                displacement,
                settings,
                blockingEntity,
                blockingPoint,
                blockingNormal) == false)
        {
            previousFraction = currentFraction;
            continue;
        }

        // ====================================================================
        // Time Of Impact refinement
        // ====================================================================
        // サブステップで初めてOverlapした区間 [previous, current] を二分探索し、
        // 最初の接触時刻を絞り込みます。毎回同じNarrow Phaseを使うためShapeごとのSweep式を
        // 個別実装せず、既存Sphere/Box/Capsule/Plane接触判定を一貫して再利用できます。
        float low = previousFraction;
        float high = currentFraction;
        Entity refinedEntity = blockingEntity;
        math::Vec3 refinedPoint = blockingPoint;
        math::Vec3 refinedNormal = blockingNormal;

        for (uint32_t iteration = 0u; iteration < settings.BinarySearchIterations; ++iteration)
        {
            const float middle = (low + high) * 0.5f;
            const math::Vec3 middleFootPosition = startFootPosition + displacement * middle;

            Entity middleEntity{};
            math::Vec3 middlePoint{};
            math::Vec3 middleNormal{};
            if (FindBlockingOverlap(
                    scene,
                    middleFootPosition,
                    displacement,
                    settings,
                    middleEntity,
                    middlePoint,
                    middleNormal) == true)
            {
                high = middle;
                refinedEntity = middleEntity;
                refinedPoint = middlePoint;
                refinedNormal = middleNormal;
            }
            else
            {
                low = middle;
            }
        }

        outHit = PhysicsCapsuleCastHit{};
        outHit.HitEntity = refinedEntity;
        outHit.Fraction = std::clamp(high, 0.0f, 1.0f);
        outHit.Position = startFootPosition + displacement * outHit.Fraction;
        outHit.Point = refinedPoint;
        outHit.Normal = refinedNormal;
        return true;
    }

    return false;
}

} // namespace Raven::ph
