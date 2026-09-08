#include <cassert>
#include <cmath>
#include <memory>
#include <vector>

#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/Capsule.h"
#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"
#include "Raven/Scene/Scene.h"

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

std::shared_ptr<MeshGeometry> MakeStaticMeshGroundGeometry()
{
    std::vector<MeshVertex> vertices{
        MeshVertex{ math::Vec3{ -2.0f, 0.0f, -2.0f } },
        MeshVertex{ math::Vec3{  2.0f, 0.0f, -2.0f } },
        MeshVertex{ math::Vec3{  2.0f, 0.0f,  2.0f } },
        MeshVertex{ math::Vec3{ -2.0f, 0.0f,  2.0f } }
    };
    std::vector<uint32_t> indices{ 0u, 1u, 2u, 0u, 2u, 3u };
    return std::make_shared<MeshGeometry>(std::move(vertices), std::move(indices));
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
    // 2. Segment - Triangle closest points
    // ------------------------------------------------------------------------
    // Terrain Triangleの面領域へ垂直に近づく場合と、中心線分が面を貫く場合を検証します。
    const math::Vec3 triangleA{ -1.0f, 0.0f, -1.0f };
    const math::Vec3 triangleB{ 1.0f, 0.0f, -1.0f };
    const math::Vec3 triangleC{ 0.0f, 0.0f, 1.0f };
    math::Vec3 segmentPoint{};
    math::Vec3 trianglePoint{};

    ClosestPointsSegmentTriangle(
        math::Vec3{ 0.0f, 0.25f, 0.0f },
        math::Vec3{ 0.0f, 1.0f, 0.0f },
        triangleA,
        triangleB,
        triangleC,
        segmentPoint,
        trianglePoint);
    assert(NearlyEqual(segmentPoint.y, 0.25f));
    assert(NearlyEqual(trianglePoint.y, 0.0f));
    assert(NearlyEqual((segmentPoint - trianglePoint).Length(), 0.25f));

    ClosestPointsSegmentTriangle(
        math::Vec3{ 0.0f, -1.0f, 0.0f },
        math::Vec3{ 0.0f, 1.0f, 0.0f },
        triangleA,
        triangleB,
        triangleC,
        segmentPoint,
        trianglePoint);
    assert((segmentPoint - trianglePoint).LengthSq() <= 1.0e-10f);
    assert(NearlyEqual(segmentPoint.y, 0.0f));

    // ------------------------------------------------------------------------
    // 3. Sphere - Capsule
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
    // 4. Capsule - Capsule
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
    // 5. Capsule - Plane
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
    // 6. Capsule - Box
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
    // 7. Capsule - Static Mesh Manifold
    // ------------------------------------------------------------------------
    // Capsule下端をTerrain面へ0.1だけ貫通させ、法線・貫通量・Material合成を確認します。
    ColliderComponent staticMeshCollider{};
    staticMeshCollider.Type = ColliderType::StaticMesh;
    staticMeshCollider.StaticMeshGeometry = MakeStaticMeshGroundGeometry();
    staticMeshCollider.StaticFriction = 0.2f;
    staticMeshCollider.DynamicFriction = 0.45f;
    staticMeshCollider.Restitution = 0.1f;

    capsuleCollider.StaticFriction = 0.8f;
    capsuleCollider.DynamicFriction = 0.2f;
    capsuleCollider.Restitution = 0.5f;
    capsuleTransform = TransformComponent{};
    capsuleTransform.Position = { 0.0f, 1.4f, 0.0f };
    TransformComponent staticMeshTransform{};

    assert(GenerateCapsuleStaticMeshManifold(
        Entity{}, capsuleTransform, capsuleCollider,
        Entity{}, staticMeshTransform, staticMeshCollider,
        manifold));
    assert(manifold.PointCount == 1u);
    assert(NearlyEqual(manifold.Points[0].Penetration, 0.1f, 1.0e-3f));
    assert(manifold.Normal.y < -0.99f);
    assert(NearlyEqual(manifold.StaticFriction, 0.4f));
    assert(NearlyEqual(manifold.DynamicFriction, 0.3f));
    assert(NearlyEqual(manifold.Restitution, 0.1f));

    capsuleTransform.Position = { 0.0f, 2.0f, 0.0f };
    assert(GenerateCapsuleStaticMeshManifold(
        Entity{}, capsuleTransform, capsuleCollider,
        Entity{}, staticMeshTransform, staticMeshCollider,
        manifold) == false);

    // ------------------------------------------------------------------------
    // 8. PhysicsWorld dispatch - Dynamic Capsule / Static Mesh
    // ------------------------------------------------------------------------
    // Direct Narrow PhaseだけでなくBroad Phase -> PhysicsWorld dispatch -> Manifoldまで
    // 実際のStep経路で到達することを確認します。
    {
        Scene scene;
        PhysicsWorld world;
        world.SetGravity({ 0.0f, 0.0f, 0.0f });

        Entity terrain = scene.CreateEntity("CapsuleStaticMeshSelfTestTerrain");
        ColliderComponent& terrainCollider = terrain.AddComponent<ColliderComponent>();
        terrainCollider.Type = ColliderType::StaticMesh;
        terrainCollider.StaticMeshGeometry = MakeStaticMeshGroundGeometry();

        Entity capsuleEntity = scene.CreateEntity("CapsuleStaticMeshSelfTestCapsule");
        capsuleEntity.GetComponent<TransformComponent>().Position = { 0.0f, 1.4f, 0.0f };
        ColliderComponent& dynamicCapsuleCollider = capsuleEntity.AddComponent<ColliderComponent>();
        dynamicCapsuleCollider = MakeCapsule(0.5f, 1.0f);
        RigidBodyComponent& rigidBody = capsuleEntity.AddComponent<RigidBodyComponent>();
        rigidBody.SetBodyType(BodyType::Dynamic);
        rigidBody.UseGravity = false;
        rigidBody.AllowSleep = false;

        world.Step(scene, 1.0f / 60.0f);
        const auto& manifolds = world.GetContactManifolds();
        assert(manifolds.empty() == false);

        bool foundTerrainContact = false;
        for (const ContactManifold& contact : manifolds)
        {
            if (contact.A == capsuleEntity && contact.B == terrain)
            {
                foundTerrainContact = true;
                assert(contact.PointCount > 0u);
                assert(contact.Normal.y < -0.99f);
                break;
            }
        }
        assert(foundTerrainContact);
    }

    // ------------------------------------------------------------------------
    // 9. Ray - Capsule
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