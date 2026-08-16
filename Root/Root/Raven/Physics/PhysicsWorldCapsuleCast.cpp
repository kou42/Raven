// Raven/Physics/PhysicsWorldCapsuleCast.cpp
#include "Raven/Physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Raven/Physics/Collision/CollisionDetection.h"
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
