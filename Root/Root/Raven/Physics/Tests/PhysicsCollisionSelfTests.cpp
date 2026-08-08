#include <cassert>
#include <cmath>
#include <vector>

#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Collision/DynamicAABBTreeValidation.h"
#include "Raven/Physics/Collision/OBB.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/RigidBodyDynamics.h"
#include "Raven/Physics/Solver/ContactSolver.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph::tests
{
namespace
{

bool NearlyEqual(float a, float b, float epsilon = 1.0e-5f)
{
    return std::abs(a - b) <= epsilon;
}

bool IsFinite(const math::Vec3& value)
{
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
}

Entity CreateBox(Scene& scene, const math::Vec3& position, BodyType type)
{
    Entity entity = scene.CreateEntity("PhysicsSelfTestBox");

    auto& transform = entity.GetComponent<TransformComponent>();
    transform.Position = position;

    auto& collider = entity.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::Box;
    collider.HalfExtents = { 0.5f, 0.5f, 0.5f };
    collider.Restitution = 0.0f;
    collider.StaticFriction = 0.7f;
    collider.DynamicFriction = 0.5f;

    auto& body = entity.AddComponent<RigidBodyComponent>();
    body.SetBodyType(type);
    body.LinearDamping = 0.01f;
    body.AngularDamping = 0.01f;
    body.AllowSleep = false;

    return entity;
}

struct StackResult
{
    float MaxSpeed = 0.0f;
    float TopHeight = 0.0f;
    float MaximumPenetration = 0.0f;
    bool AllFinite = true;
    std::size_t PersistentImpulseFrames = 0;
};

StackResult RunBoxStackScenario(bool warmStart, uint32_t iterations)
{
    Scene scene;
    PhysicsWorld world;

    ContactSolverSettings settings{};
    settings.EnableWarmStart = warmStart;
    settings.VelocityIterations = iterations;
    world.SetSolverSettings(settings);

    Entity floor = CreateBox(scene, { 0.0f, -0.5f, 0.0f }, BodyType::Static);
    floor.GetComponent<ColliderComponent>().HalfExtents = { 5.0f, 0.5f, 5.0f };

    std::vector<Entity> boxes;
    for (int i = 0; i < 8; ++i)
    {
        boxes.push_back(CreateBox(scene, { 0.0f, 0.5f + i * 1.002f, 0.0f }, BodyType::Dynamic));
    }

    StackResult result{};

    // 10秒相当のステップを回し、安定性・最大貫通・Warm Start継続性を計測します。
    for (int step = 0; step < 600; ++step)
    {
        world.Step(scene, 1.0f / 60.0f);

        for (Entity box : boxes)
        {
            const auto& transform = box.GetComponent<TransformComponent>();
            const auto& body = box.GetComponent<RigidBodyComponent>();

            result.AllFinite = result.AllFinite
                && IsFinite(transform.Position)
                && IsFinite(body.LinearVelocity)
                && IsFinite(body.AngularVelocity);
            result.MaxSpeed = std::max(result.MaxSpeed, std::sqrt(body.LinearVelocity.LengthSq()));
        }

        bool foundImpulse = false;
        for (const auto& manifold : world.GetContactManifolds())
        {
            for (std::size_t i = 0; i < manifold.PointCount; ++i)
            {
                result.MaximumPenetration = std::max(
                    result.MaximumPenetration,
                    manifold.Points[i].Penetration);

                if (manifold.Points[i].AccumulatedNormalImpulse > 1.0e-5f)
                {
                    foundImpulse = true;
                }
            }
        }

        if (foundImpulse)
        {
            ++result.PersistentImpulseFrames;
        }
    }

    result.TopHeight = boxes.back().GetComponent<TransformComponent>().Position.y;
    return result;
}

} // namespace

void RunDynamicAABBTreeSelfTests()
{
    // 動的ツリーの作成・移動・削除と妥当性検証が壊れていないことを確認します。
    DynamicAABBTree tree;

    const uint32_t a = tree.CreateProxy(AABB{ { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f } }, Entity{});
    const uint32_t b = tree.CreateProxy(AABB{ { 3.0f, -1.0f, -1.0f }, { 5.0f, 1.0f, 1.0f } }, Entity{});
    const uint32_t c = tree.CreateProxy(AABB{ { 7.0f, -1.0f, -1.0f }, { 9.0f, 1.0f, 1.0f } }, Entity{});

    assert(ValidateDynamicAABBTree(tree).IsValid());
    assert(tree.MoveProxy(a, AABB{ { -0.95f, -1.0f, -1.0f }, { 1.05f, 1.0f, 1.0f } }, { 0.05f, 0.0f, 0.0f }) == false);
    assert(tree.MoveProxy(b, AABB{ { 20.0f, -1.0f, -1.0f }, { 22.0f, 1.0f, 1.0f } }, { 17.0f, 0.0f, 0.0f }));

    uint32_t count = 0;
    tree.Query(AABB{ { -2.0f, -2.0f, -2.0f }, { 2.0f, 2.0f, 2.0f } },
        [&](Entity, uint32_t proxy)
        {
            if (proxy == a)
            {
                ++count;
            }
            return true;
        });

    assert(count == 1);

    tree.DestroyProxy(c);
    tree.DestroyProxy(b);
    tree.DestroyProxy(a);
    assert(ValidateDynamicAABBTree(tree).IsValid());
}

void RunOBBFoundationSelfTests()
{
    // OBB基礎: 回転後の軸が正規化され、姿勢計算が崩れていないことを確認します。
    ColliderComponent collider{};
    collider.Type = ColliderType::Box;
    collider.HalfExtents = { 1.0f, 0.5f, 0.25f };

    TransformComponent transform{};
    transform.Rotation = { 0.0f, 0.0f, 0.785398163f };

    OBB box{};
    assert(ComputeBoxOBB(transform, collider, box));
    assert(NearlyEqual(box.Axis[0].Length(), 1.0f, 1.0e-4f));
}

void RunOBBRayCastSelfTests()
{
    // OBBとAABBのレイ判定差分を使い、回転Box専用判定が効いていることを確認します。
    TransformComponent transform{};
    transform.Rotation = { 0.0f, 0.0f, 0.785398163f };

    ColliderComponent collider{};
    collider.Type = ColliderType::Box;
    collider.HalfExtents = { 1.0f, 0.25f, 0.5f };

    OBB box{};
    assert(ComputeBoxOBB(transform, collider, box));

    float fraction = 0.0f;
    math::Vec3 normal{};

    assert(box.RayCast({ -2.0f, -2.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, 10.0f, fraction, &normal));

    AABB aabb{};
    assert(ComputeColliderAABB(transform, collider, aabb));

    const math::Vec3 start{ aabb.Max.x - 0.02f, -2.0f, 0.0f };
    assert(aabb.RayCast(start, { 0.0f, 1.0f, 0.0f }, 10.0f, fraction, &normal));
    assert(box.RayCast(start, { 0.0f, 1.0f, 0.0f }, 10.0f, fraction, &normal) == false);
}

void RunSphereBoxSelfTests()
{
    // 球-箱の衝突/非衝突境界でマニホールド生成可否を検証します。
    ColliderComponent sphere{};
    sphere.Type = ColliderType::Sphere;
    sphere.Radius = 1.0f;

    ColliderComponent box{};
    box.Type = ColliderType::Box;
    box.HalfExtents = { 1.0f, 1.0f, 1.0f };

    TransformComponent boxTransform{};
    TransformComponent sphereTransform{};
    ContactManifold manifold{};

    sphereTransform.Position = { 1.5f, 0.0f, 0.0f };
    assert(GenerateSphereBoxManifold(Entity{}, sphereTransform, sphere, Entity{}, boxTransform, box, manifold));

    sphereTransform.Position = { 4.0f, 0.0f, 0.0f };
    assert(GenerateSphereBoxManifold(Entity{}, sphereTransform, sphere, Entity{}, boxTransform, box, manifold) == false);
}

void RunBoxBoxSelfTests()
{
    // 箱-箱の基本重なり判定が、距離に応じて正しく切り替わることを検証します。
    ColliderComponent a{};
    a.Type = ColliderType::Box;
    a.HalfExtents = { 1.0f, 1.0f, 1.0f };

    ColliderComponent b = a;

    TransformComponent transformA{};
    TransformComponent transformB{};
    ContactManifold manifold{};

    transformB.Position = { 1.5f, 0.0f, 0.0f };
    assert(GenerateBoxBoxManifold(Entity{}, transformA, a, Entity{}, transformB, b, manifold));

    transformB.Position = { 3.0f, 0.0f, 0.0f };
    assert(GenerateBoxBoxManifold(Entity{}, transformA, a, Entity{}, transformB, b, manifold) == false);
}

void RunContactFeatureIDSelfTests()
{
    // 永続接触キーとして使うFeature IDが、回転後も生成されることを検証します。
    ColliderComponent a{};
    a.Type = ColliderType::Box;
    a.HalfExtents = { 1.0f, 1.0f, 1.0f };

    ColliderComponent b = a;

    TransformComponent transformA{};
    TransformComponent transformB{};
    transformB.Position = { 1.5f, 0.0f, 0.0f };

    ContactManifold manifold{};
    assert(GenerateBoxBoxManifold(Entity{}, transformA, a, Entity{}, transformB, b, manifold));
    assert(manifold.PointCount > 0);

    for (std::size_t i = 0; i < manifold.PointCount; ++i)
    {
        // Feature IDは接触点の幾何対応を保持し、永続接触の再対応付けに使われます。
        assert(manifold.Points[i].Feature.IsValid());
        assert((manifold.Points[i].Feature.TypeA == ContactFeatureType::Face
            && manifold.Points[i].Feature.TypeB == ContactFeatureType::Vertex)
            || (manifold.Points[i].Feature.TypeA == ContactFeatureType::Vertex
                && manifold.Points[i].Feature.TypeB == ContactFeatureType::Face));
    }

    transformB.Rotation = { 0.0f, 0.0f, 0.08f };
    ContactManifold rotated{};

    assert(GenerateBoxBoxManifold(Entity{}, transformA, a, Entity{}, transformB, b, rotated));
    assert(rotated.PointCount > 0);

    for (std::size_t i = 0; i < rotated.PointCount; ++i)
    {
        assert(rotated.Points[i].Feature.IsValid());
    }
}

void RunAngularDynamicsSelfTests()
{
    // トルク入力が角速度へ反映される最短経路を確認します。
    Scene scene;
    PhysicsWorld world;
    world.SetGravity({ 0.0f, 0.0f, 0.0f });

    Entity entity = CreateBox(scene, { 0.0f, 0.0f, 0.0f }, BodyType::Dynamic);
    auto& body = entity.GetComponent<RigidBodyComponent>();

    world.AddTorque(scene, entity, { 0.0f, 0.0f, 1.0f });
    world.Step(scene, 1.0f / 60.0f);

    assert(body.AngularVelocity.z > 0.0f);
}

void RunPointImpulseSelfTests()
{
    // 対称な点インパルスで並進が相殺され、回転のみ残ることを確認します。
    Scene scene;
    PhysicsWorld world;

    Entity entity = CreateBox(scene, { 0.0f, 0.0f, 0.0f }, BodyType::Dynamic);
    auto& body = entity.GetComponent<RigidBodyComponent>();

    world.AddImpulseAtPoint(scene, entity, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.5f, 0.0f });
    world.AddImpulseAtPoint(scene, entity, { -1.0f, 0.0f, 0.0f }, { 0.0f, -0.5f, 0.0f });

    assert(body.LinearVelocity.LengthSq() <= 1.0e-12f);
    assert(std::abs(body.AngularVelocity.z) > 1.0e-5f);
}

void RunAngularPositionSolverSelfTests()
{
    // 位置補正フェーズで回転補正が必要なケース/不要なケースを分けて検証します。
    ContactSolverSettings settings{};
    settings.EnableWarmStart = false;
    settings.VelocityIterations = 1;
    settings.PositionIterations = 4;
    settings.PositionCorrectionPercent = 0.8f;
    settings.PenetrationSlop = 0.0f;

    {
        Scene scene;
        Entity a = CreateBox(scene, { 0.0f, 0.0f, 0.0f }, BodyType::Dynamic);
        Entity b = CreateBox(scene, { 0.0f, 0.0f, 0.0f }, BodyType::Static);

        auto& body = a.GetComponent<RigidBodyComponent>();
        auto& transform = a.GetComponent<TransformComponent>();

        ContactManifold manifold{};
        manifold.A = a;
        manifold.B = b;
        manifold.Normal = { 1.0f, 0.0f, 0.0f };
        manifold.PointCount = 1;
        manifold.Points[0].Position = { 0.0f, 0.5f, 0.0f };
        manifold.Points[0].Penetration = 0.2f;

        std::vector<ContactManifold> manifolds{ manifold };
        SolveContactManifolds(scene, manifolds, 1.0f / 60.0f, settings);

        assert(transform.Position.x < 0.0f);
        assert(body.OrientationInitialized);
        assert(std::abs(transform.Rotation.z) > 1.0e-5f);
        assert(body.LinearVelocity.LengthSq() <= 1.0e-12f);
        assert(body.AngularVelocity.LengthSq() <= 1.0e-12f);
        assert(manifolds[0].Points[0].Penetration < 0.2f);
    }

    {
        Scene scene;
        Entity a = CreateBox(scene, { 0.0f, 0.0f, 0.0f }, BodyType::Dynamic);
        Entity b = CreateBox(scene, { 0.0f, 0.0f, 0.0f }, BodyType::Static);

        auto& body = a.GetComponent<RigidBodyComponent>();
        auto& transform = a.GetComponent<TransformComponent>();

        ContactManifold manifold{};
        manifold.A = a;
        manifold.B = b;
        manifold.Normal = { 1.0f, 0.0f, 0.0f };
        manifold.PointCount = 1;
        manifold.Points[0].Position = { 0.0f, 0.0f, 0.0f };
        manifold.Points[0].Penetration = 0.2f;

        std::vector<ContactManifold> manifolds{ manifold };
        SolveContactManifolds(scene, manifolds, 1.0f / 60.0f, settings);

        assert(transform.Position.x < 0.0f);
        assert(std::abs(transform.Rotation.z) <= 1.0e-6f);
        assert(body.AngularVelocity.LengthSq() <= 1.0e-12f);
    }
}

void RunContactAnchorPositionSelfTests()
{
    // ローカルアンカー固定により、反復回数増加で貫通が減ることを検証します。
    auto solve = [](uint32_t iterations)
    {
        Scene scene;
        Entity a = CreateBox(scene, { 0.0f, 0.0f, 0.0f }, BodyType::Dynamic);
        Entity b = CreateBox(scene, { 0.0f, 0.0f, 0.0f }, BodyType::Static);

        ContactManifold manifold{};
        manifold.A = a;
        manifold.B = b;
        manifold.Normal = { 1.0f, 0.0f, 0.0f };
        manifold.PointCount = 1;
        manifold.Points[0].Position = { 0.0f, 0.5f, 0.0f };
        manifold.Points[0].Penetration = 0.25f;

        ContactSolverSettings settings{};
        settings.EnableWarmStart = false;
        settings.VelocityIterations = 1;
        settings.PositionIterations = iterations;
        settings.PositionCorrectionPercent = 0.5f;
        settings.PenetrationSlop = 0.0f;

        std::vector<ContactManifold> manifolds{ manifold };
        SolveContactManifolds(scene, manifolds, 1.0f / 60.0f, settings);

        assert(manifolds[0].Points[0].PositionAnchorsInitialized);
        assert(IsFinite(a.GetComponent<TransformComponent>().Position));
        assert(IsFinite(a.GetComponent<RigidBodyComponent>().AngularVelocity));

        return manifolds[0].Points[0].Penetration;
    };

    const float afterOne = solve(1);
    const float afterFour = solve(4);

    assert(afterOne < 0.25f);
    assert(afterFour < afterOne);
    assert(afterFour >= 0.0f);
}

void RunContactPersistenceWarmStartSelfTests()
{
    // Warm Start有効時に、積み上げ安定性と接触継続が改善することを確認します。
    const StackResult cold = RunBoxStackScenario(false, 1);
    const StackResult warm = RunBoxStackScenario(true, 1);

    assert(cold.AllFinite && warm.AllFinite);
    assert(warm.PersistentImpulseFrames > 0);
    assert(warm.TopHeight > 6.5f && warm.TopHeight < 8.5f);
    assert(warm.MaximumPenetration <= cold.MaximumPenetration + 0.05f);
}

void RunBoxStackStressTest()
{
    // 高反復設定で長時間ステップしても数値破綻しないことを確認します。
    const StackResult result = RunBoxStackScenario(true, 8);

    assert(result.AllFinite);
    assert(result.PersistentImpulseFrames > 100);
    assert(result.TopHeight > 6.5f && result.TopHeight < 8.5f);
    assert(result.MaximumPenetration < 0.25f);
}

void RunPhysicsCollisionSelfTests()
{
    // Collisionまわりの回帰テストを一括実行します。
    RunDynamicAABBTreeSelfTests();
    RunOBBFoundationSelfTests();
    RunOBBRayCastSelfTests();
    RunSphereBoxSelfTests();
    RunBoxBoxSelfTests();
    RunContactFeatureIDSelfTests();
    RunAngularDynamicsSelfTests();
    RunPointImpulseSelfTests();
    RunAngularPositionSolverSelfTests();
    RunContactAnchorPositionSelfTests();
    RunContactPersistenceWarmStartSelfTests();
    RunBoxStackStressTest();
}

} // namespace Raven::ph::tests
