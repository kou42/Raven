#include <cassert>
#include <cmath>

#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/Capsule.h"
#include "Raven/Physics/Collision/CollisionDetection.h"

namespace Raven::ph::tests
{

namespace
{

bool NearlyEqual(float a, float b, float epsilon = 1.0e-4f)
{
    return std::abs(a - b) <= epsilon;
}

ColliderComponent MakeCapsule(float radius, float halfLength)
{
    ColliderComponent collider{};
    collider.Type = ColliderType::Capsule;
    collider.Radius = radius;
    collider.HalfLength = halfLength;
    return collider;
}

} // namespace

// ============================================================================
// Capsule Collider Self Tests
// ============================================================================
// この関数は既存PhysicsCollisionSelfTestsと同じく、assertベースで手動実行できる
// 回帰テスト群です。Capsuleの形状契約・Broad Phase AABB・主要Narrow Phase・RayCastを
// 独立して確認できるようにしています。
void RunCapsuleCollisionSelfTests()
{
    // ------------------------------------------------------------------------
    // 1. 基礎形状と回転
    // ------------------------------------------------------------------------
    // HalfLength=1, Radius=0.5なら、無回転時の中心線分はY=-1～+1、全高は3です。
    ColliderComponent capsuleCollider = MakeCapsule(0.5f, 1.0f);
    TransformComponent capsuleTransform{};

    Capsule capsule{};
    assert(ComputeCapsule(capsuleTransform, capsuleCollider, capsule));
    assert(NearlyEqual(capsule.SegmentA.y, -1.0f));
    assert(NearlyEqual(capsule.SegmentB.y, 1.0f));
    assert(NearlyEqual(capsule.Radius, 0.5f));

    AABB bounds{};
    assert(ComputeColliderAABB(capsuleTransform, capsuleCollider, bounds));
    assert(NearlyEqual(bounds.Min.x, -0.5f));
    assert(NearlyEqual(bounds.Min.y, -1.5f));
    assert(NearlyEqual(bounds.Max.y, 1.5f));
    assert(NearlyEqual(bounds.Max.z, 0.5f));

    // Z軸へ90度回すとローカルY軸はworld X方向へ向きます。
    capsuleTransform.Rotation = { 0.0f, 0.0f, 1.57079632679f };
    assert(ComputeCapsule(capsuleTransform, capsuleCollider, capsule));
    assert(NearlyEqual(std::abs(capsule.SegmentA.x), 1.0f, 1.0e-3f));
    assert(NearlyEqual(std::abs(capsule.SegmentB.x), 1.0f, 1.0e-3f));

    // ------------------------------------------------------------------------
    // 2. Sphere - Capsule
    // ------------------------------------------------------------------------
    ColliderComponent sphereCollider{};
    sphereCollider.Type = ColliderType::Sphere;
    sphereCollider.Radius = 0.5f;

    TransformComponent sphereTransform{};
    capsuleTransform = TransformComponent{};
    ContactManifold manifold{};

    sphereTransform.Position = { 0.8f, 0.0f, 0.0f };
    assert(GenerateSphereCapsuleManifold(
        Entity{}, sphereTransform, sphereCollider,
        Entity{}, capsuleTransform, capsuleCollider,
        manifold));
    assert(manifold.PointCount == 1);
    assert(manifold.Points[0].Penetration > 0.0f);

    sphereTransform.Position = { 2.0f, 0.0f, 0.0f };
    assert(GenerateSphereCapsuleManifold(
        Entity{}, sphereTransform, sphereCollider,
        Entity{}, capsuleTransform, capsuleCollider,
        manifold) == false);

    // ------------------------------------------------------------------------
    // 3. Capsule - Capsule
    // ------------------------------------------------------------------------
    TransformComponent capsuleTransformA{};
    TransformComponent capsuleTransformB{};
    capsuleTransformB.Position = { 0.8f, 0.0f, 0.0f };
    assert(GenerateCapsuleCapsuleManifold(
        Entity{}, capsuleTransformA, capsuleCollider,
        Entity{}, capsuleTransformB, capsuleCollider,
        manifold));

    capsuleTransformB.Position = { 2.0f, 0.0f, 0.0f };
    assert(GenerateCapsuleCapsuleManifold(
        Entity{}, capsuleTransformA, capsuleCollider,
        Entity{}, capsuleTransformB, capsuleCollider,
        manifold) == false);

    // ------------------------------------------------------------------------
    // 4. Capsule - Plane
    // ------------------------------------------------------------------------
    ColliderComponent planeCollider{};
    planeCollider.Type = ColliderType::Plane;
    planeCollider.PlaneNormal = { 0.0f, 1.0f, 0.0f };
    TransformComponent planeTransform{};

    capsuleTransform = TransformComponent{};
    capsuleTransform.Position = { 0.0f, 1.4f, 0.0f };
    assert(GenerateCapsulePlaneManifold(
        Entity{}, capsuleTransform, capsuleCollider,
        Entity{}, planeTransform, planeCollider,
        manifold));
    assert(NearlyEqual(manifold.Points[0].Penetration, 0.1f, 1.0e-3f));
    assert(manifold.Normal.y < 0.0f);

    capsuleTransform.Position = { 0.0f, 2.0f, 0.0f };
    assert(GenerateCapsulePlaneManifold(
        Entity{}, capsuleTransform, capsuleCollider,
        Entity{}, planeTransform, planeCollider,
        manifold) == false);

    // ------------------------------------------------------------------------
    // 5. Capsule - Box
    // ------------------------------------------------------------------------
    ColliderComponent boxCollider{};
    boxCollider.Type = ColliderType::Box;
    boxCollider.HalfExtents = { 0.5f, 0.5f, 0.5f };
    TransformComponent boxTransform{};

    capsuleTransform = TransformComponent{};
    capsuleTransform.Position = { 0.8f, 0.0f, 0.0f };
    assert(GenerateCapsuleBoxManifold(
        Entity{}, capsuleTransform, capsuleCollider,
        Entity{}, boxTransform, boxCollider,
        manifold));

    capsuleTransform.Position = { 2.0f, 0.0f, 0.0f };
    assert(GenerateCapsuleBoxManifold(
        Entity{}, capsuleTransform, capsuleCollider,
        Entity{}, boxTransform, boxCollider,
        manifold) == false);

    // ------------------------------------------------------------------------
    // 6. Ray - Capsule
    // ------------------------------------------------------------------------
    capsuleTransform = TransformComponent{};
    assert(ComputeCapsule(capsuleTransform, capsuleCollider, capsule));

    float fraction = 0.0f;
    math::Vec3 normal{};
    assert(RayCastCapsule(
        { -2.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f },
        10.0f,
        capsule,
        fraction,
        normal));
    assert(NearlyEqual(fraction, 1.5f, 1.0e-3f));
    assert(normal.x < 0.0f);

    assert(RayCastCapsule(
        { -2.0f, 3.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f },
        10.0f,
        capsule,
        fraction,
        normal) == false);
}

} // namespace Raven::ph::tests
