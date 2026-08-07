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

// Visual StudioのTest Adapterへ依存しない最小Self Test群です。
// 現時点では通常アプリのビルド対象には登録せず、次に専用Test Targetを作る際の
// テスト本体として分離しています。assertが有効なDebug構成で利用します。
void RunDynamicAABBTreeSelfTests()
{
    DynamicAABBTree tree;

    const uint32_t a = tree.CreateProxy(AABB{ {-1,-1,-1}, {1,1,1} }, Entity{});
    const uint32_t b = tree.CreateProxy(AABB{ {3,-1,-1}, {5,1,1} }, Entity{});
    const uint32_t c = tree.CreateProxy(AABB{ {7,-1,-1}, {9,1,1} }, Entity{});

    assert(ValidateDynamicAABBTree(tree).IsValid());

    // Fat AABB内の微小移動は再挿入しないこと。
    assert(!tree.MoveProxy(a, AABB{ {-0.95f,-1,-1}, {1.05f,1,1} }, {0.05f,0,0}));
    assert(ValidateDynamicAABBTree(tree).IsValid());

    // Fat AABB外への移動では再挿入されてもTree構造が壊れないこと。
    assert(tree.MoveProxy(b, AABB{ {20,-1,-1}, {22,1,1} }, {17,0,0}));
    assert(ValidateDynamicAABBTree(tree).IsValid());

    uint32_t queryCount = 0;
    tree.Query(AABB{ {-2,-2,-2}, {2,2,2} },
        [&](Entity, uint32_t proxy) -> bool
        {
            if (proxy == a) ++queryCount;
            return true;
        });
    assert(queryCount == 1);

    tree.DestroyProxy(c);
    tree.DestroyProxy(b);
    tree.DestroyProxy(a);
    assert(ValidateDynamicAABBTree(tree).IsValid());
}

void RunSphereBoxSelfTests()
{
    ColliderComponent sphere{};
    sphere.Type = ColliderType::Sphere;
    sphere.Radius = 1.0f;

    ColliderComponent box{};
    box.Type = ColliderType::Box;
    box.HalfExtents = { 1.0f, 1.0f, 1.0f };

    TransformComponent boxTransform{};
    ContactManifold manifold{};

    // +X側から0.5だけ貫通。
    TransformComponent sphereTransform{};
    sphereTransform.Position = { 1.5f, 0.0f, 0.0f };
    assert(GenerateSphereBoxManifold(Entity{}, sphereTransform, sphere,
        Entity{}, boxTransform, box, manifold));
    assert(manifold.PointCount == 1);
    assert(NearlyEqual(manifold.Points[0].Penetration, 0.5f));
    assert(NearlyEqual(manifold.Normal.x, -1.0f));

    // Sphere中心がBox中心にある内部ケース。
    // tie時は-X面を選び、Sphereを-Xへ押し出すためNormalは+Xになります。
    sphereTransform.Position = {};
    assert(GenerateSphereBoxManifold(Entity{}, sphereTransform, sphere,
        Entity{}, boxTransform, box, manifold));
    assert(NearlyEqual(manifold.Points[0].Penetration, 2.0f));
    assert(NearlyEqual(manifold.Normal.x, 1.0f));

    // 十分離れていればManifoldは生成されないこと。
    sphereTransform.Position = { 4.0f, 0.0f, 0.0f };
    assert(!GenerateSphereBoxManifold(Entity{}, sphereTransform, sphere,
        Entity{}, boxTransform, box, manifold));
}

} // namespace Raven::ph::tests
