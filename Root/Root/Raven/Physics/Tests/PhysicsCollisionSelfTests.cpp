#include <cassert>
#include <cmath>

#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Collision/DynamicAABBTreeValidation.h"

namespace Raven::ph::tests
{
namespace
{
bool NearlyEqual(float a, float b, float epsilon = 1.0e-5f)
{
    return std::abs(a - b) <= epsilon;
}
}

void RunDynamicAABBTreeSelfTests()
{
    DynamicAABBTree tree;
    const uint32_t a = tree.CreateProxy(AABB{ {-1,-1,-1}, {1,1,1} }, Entity{});
    const uint32_t b = tree.CreateProxy(AABB{ {3,-1,-1}, {5,1,1} }, Entity{});
    const uint32_t c = tree.CreateProxy(AABB{ {7,-1,-1}, {9,1,1} }, Entity{});
    assert(ValidateDynamicAABBTree(tree).IsValid());

    assert(!tree.MoveProxy(a, AABB{ {-0.95f,-1,-1}, {1.05f,1,1} }, {0.05f,0,0}));
    assert(ValidateDynamicAABBTree(tree).IsValid());
    assert(tree.MoveProxy(b, AABB{ {20,-1,-1}, {22,1,1} }, {17,0,0}));
    assert(ValidateDynamicAABBTree(tree).IsValid());

    uint32_t queryCount = 0;
    tree.Query(AABB{ {-2,-2,-2}, {2,2,2} }, [&](Entity, uint32_t proxy) -> bool
    {
        if (proxy == a) ++queryCount;
        return true;
    });
    assert(queryCount == 1);

    tree.DestroyProxy(c); tree.DestroyProxy(b); tree.DestroyProxy(a);
    assert(ValidateDynamicAABBTree(tree).IsValid());
}

void RunSphereBoxSelfTests()
{
    ColliderComponent sphere{}; sphere.Type = ColliderType::Sphere; sphere.Radius = 1.0f;
    ColliderComponent box{}; box.Type = ColliderType::Box; box.HalfExtents = {1,1,1};
    TransformComponent boxTransform{};
    TransformComponent sphereTransform{};
    ContactManifold manifold{};

    sphereTransform.Position = {1.5f,0,0};
    assert(GenerateSphereBoxManifold(Entity{}, sphereTransform, sphere, Entity{}, boxTransform, box, manifold));
    assert(manifold.PointCount == 1);
    assert(NearlyEqual(manifold.Points[0].Penetration, 0.5f));
    assert(NearlyEqual(manifold.Normal.x, -1.0f));

    sphereTransform.Position = {};
    assert(GenerateSphereBoxManifold(Entity{}, sphereTransform, sphere, Entity{}, boxTransform, box, manifold));
    assert(NearlyEqual(manifold.Points[0].Penetration, 2.0f));
    assert(NearlyEqual(manifold.Normal.x, 1.0f));

    sphereTransform.Position = {4,0,0};
    assert(!GenerateSphereBoxManifold(Entity{}, sphereTransform, sphere, Entity{}, boxTransform, box, manifold));
}

void RunBoxBoxSelfTests()
{
    ColliderComponent aCollider{};
    aCollider.Type = ColliderType::Box;
    aCollider.HalfExtents = {1,1,1};

    ColliderComponent bCollider = aCollider;
    TransformComponent aTransform{};
    TransformComponent bTransform{};
    ContactManifold manifold{};

    // +X側から0.5貫通。Xが最小貫通軸となり、A->B法線は+Xです。
    bTransform.Position = {1.5f,0,0};
    assert(GenerateBoxBoxManifold(Entity{}, aTransform, aCollider,
        Entity{}, bTransform, bCollider, manifold));
    assert(manifold.PointCount == 4);
    assert(NearlyEqual(manifold.Normal.x, 1.0f));
    assert(NearlyEqual(manifold.Points[0].Penetration, 0.5f));

    // face-faceの重なり矩形は y,z = [-1,1] なので4 cornerを持ちます。
    assert(NearlyEqual(manifold.Points[0].Position.y, -1.0f));
    assert(NearlyEqual(manifold.Points[0].Position.z, -1.0f));
    assert(NearlyEqual(manifold.Points[2].Position.y, 1.0f));
    assert(NearlyEqual(manifold.Points[2].Position.z, 1.0f));

    // Y軸が最小貫通軸になるケース。
    bTransform.Position = {0,1.75f,0};
    assert(GenerateBoxBoxManifold(Entity{}, aTransform, aCollider,
        Entity{}, bTransform, bCollider, manifold));
    assert(NearlyEqual(manifold.Normal.y, 1.0f));
    assert(NearlyEqual(manifold.Points[0].Penetration, 0.25f));

    // 完全に離れていればSeparating Axisが存在し、Manifoldを生成しません。
    bTransform.Position = {3.0f,0,0};
    assert(!GenerateBoxBoxManifold(Entity{}, aTransform, aCollider,
        Entity{}, bTransform, bCollider, manifold));
}

void RunPhysicsCollisionSelfTests()
{
    RunDynamicAABBTreeSelfTests();
    RunSphereBoxSelfTests();
    RunBoxBoxSelfTests();
}

} // namespace Raven::ph::tests
