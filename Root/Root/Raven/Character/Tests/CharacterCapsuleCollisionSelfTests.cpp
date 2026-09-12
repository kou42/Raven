// Raven/Character/Tests/CharacterCapsuleCollisionSelfTests.cpp
#include <cassert>
#include <cmath>
#include <memory>
#include <vector>

#include "Raven/Character/CharacterController.h"
#include "Raven/Core/CPUProfiler.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::tests
{
namespace
{

Entity CreateGround(Scene& scene)
{
    Entity ground = scene.CreateEntity("CharacterCapsuleTestGround");
    ColliderComponent& collider = ground.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::Plane;
    collider.PlaneNormal = math::Vec3{ 0.0f, 1.0f, 0.0f };
    collider.IsTrigger = false;
    return ground;
}

Entity CreateWall(Scene& scene)
{
    Entity wall = scene.CreateEntity("CharacterCapsuleTestWall");
    TransformComponent& transform = wall.GetComponent<TransformComponent>();
    transform.Position = math::Vec3{ 1.25f, 1.0f, 0.0f };

    ColliderComponent& collider = wall.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::Box;
    collider.HalfExtents = math::Vec3{ 0.25f, 1.0f, 5.0f };
    collider.IsTrigger = false;
    return wall;
}

Entity CreateStaticMeshWall(Scene& scene)
{
    // x=1平面を2 Triangleで構成し、Terrainの急斜面/崖へ横から進入するケースを再現します。
    // Triangle windingへ依存せず、Capsule側へ向くObstacle Normalが得られることを検証します。
    std::vector<MeshVertex> vertices{
        MeshVertex{ math::Vec3{ 1.0f, 0.0f, -2.0f } },
        MeshVertex{ math::Vec3{ 1.0f, 2.0f, -2.0f } },
        MeshVertex{ math::Vec3{ 1.0f, 2.0f,  2.0f } },
        MeshVertex{ math::Vec3{ 1.0f, 0.0f,  2.0f } }
    };
    std::vector<uint32_t> indices{ 0u, 1u, 2u, 0u, 2u, 3u };

    Entity wall = scene.CreateEntity("CharacterCapsuleStaticMeshWall");
    ColliderComponent& collider = wall.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::StaticMesh;
    collider.StaticMeshGeometry = std::make_shared<MeshGeometry>(
        std::move(vertices),
        std::move(indices));
    collider.IsTrigger = false;
    return wall;
}

Entity CreateDynamicBox(Scene& scene, float mass)
{
    Entity box = scene.CreateEntity("CharacterCapsuleTestDynamicBox");
    TransformComponent& transform = box.GetComponent<TransformComponent>();
    transform.Position = math::Vec3{ 1.25f, 1.0f, 0.0f };

    ColliderComponent& collider = box.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::Box;
    collider.HalfExtents = math::Vec3{ 0.25f, 1.0f, 0.75f };
    collider.IsTrigger = false;

    RigidBodyComponent& rigidBody = box.AddComponent<RigidBodyComponent>();
    rigidBody.SetBodyType(BodyType::Dynamic);
    rigidBody.SetMass(mass);
    rigidBody.UseGravity = false;
    rigidBody.AllowSleep = true;
    rigidBody.IsSleeping = true;
    return box;
}

Entity CreateLowStep(Scene& scene)
{
    Entity step = scene.CreateEntity("CharacterCapsuleTestStep");
    TransformComponent& transform = step.GetComponent<TransformComponent>();
    transform.Position = math::Vec3{ 0.75f, 0.10f, 0.0f };

    ColliderComponent& collider = step.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::Box;
    collider.HalfExtents = math::Vec3{ 0.25f, 0.10f, 2.0f };
    collider.IsTrigger = false;
    return step;
}

bool NearlyEqual(float a, float b, float epsilon = 1.0e-3f)
{
    return std::fabs(a - b) <= epsilon;
}

} // namespace

void RunCharacterCapsuleCollisionSelfTests()
{
    {
        Scene scene;
        Entity wall = CreateWall(scene);
        ph::PhysicsCapsuleCastSettings settings{};
        ph::PhysicsCapsuleCastHit reference{};
        assert(scene.GetPhysicsWorld().CapsuleCast(
            scene, math::Vec3{}, math::Vec3{ 2.0f, 0.0f, 0.0f }, settings, reference));

        // 遠方Colliderを増やしてもHitとTOIは変わらず、精密判定前に除外されることを確認します。
        for (uint32_t index = 0u; index < 64u; ++index)
        {
            Entity distantWall = CreateWall(scene);
            distantWall.GetComponent<TransformComponent>().Position.x = 100.0f + index * 2.0f;
        }
        CPUProfiler& profiler = CPUProfiler::Get();
        const bool wasEnabled = profiler.IsEnabled();
        profiler.SetEnabled(true);
        profiler.BeginFrame();
        ph::PhysicsCapsuleCastHit hit{};
        assert(scene.GetPhysicsWorld().CapsuleCast(
            scene, math::Vec3{}, math::Vec3{ 2.0f, 0.0f, 0.0f }, settings, hit));
        profiler.BeginFrame();
        assert(hit.HitEntity == wall);
        assert(hit.Fraction == reference.Fraction);
        assert(hit.Normal == reference.Normal);
        assert(hit.Point == reference.Point);
        double rejectedCount = 0.0;
        for (const auto& counter : profiler.GetLastFrame().Counters)
        {
            if (counter.Name == "Physics.CapsuleCast.AABBRejectedCount")
            {
                rejectedCount += counter.Value;
            }
        }
        assert(rejectedCount >= 64.0);
        profiler.SetEnabled(wasEnabled);
    }
    // ========================================================================
    // PhysicsWorld::CapsuleCast
    // ========================================================================
    // Wall左面はx=1.0、Cast Capsule半径は0.35 + Skin 0.02です。
    // したがって足元中心Xはおよそ0.63付近で最初にBlocking Hitするはずです。
    {
        Scene scene;
        CreateWall(scene);

        ph::PhysicsCapsuleCastSettings settings{};
        settings.Radius = 0.35f;
        settings.HalfLength = 0.55f;
        settings.SkinWidth = 0.02f;

        ph::PhysicsCapsuleCastHit hit{};
        assert(scene.GetPhysicsWorld().CapsuleCast(
            scene,
            math::Vec3{ 0.0f, 0.0f, 0.0f },
            math::Vec3{ 2.0f, 0.0f, 0.0f },
            settings,
            hit));

        assert(hit.Fraction > 0.0f);
        assert(hit.Fraction < 1.0f);
        assert(hit.Position.x > 0.60f);
        assert(hit.Position.x < 0.66f);
        assert(hit.Normal.x < -0.99f);
    }

    // ========================================================================
    // PhysicsWorld::CapsuleCast - Static Mesh
    // ========================================================================
    // Box Wallと同じx=1位置をTriangle Meshで表現し、Terrainの崖面でも同じTOIと
    // 障害物法線が得られることを確認します。
    {
        Scene scene;
        Entity staticMeshWall = CreateStaticMeshWall(scene);

        ph::PhysicsCapsuleCastSettings settings{};
        settings.Radius = 0.35f;
        settings.HalfLength = 0.55f;
        settings.SkinWidth = 0.02f;

        ph::PhysicsCapsuleCastHit hit{};
        assert(scene.GetPhysicsWorld().CapsuleCast(
            scene,
            math::Vec3{ 0.0f, 0.0f, 0.0f },
            math::Vec3{ 2.0f, 0.0f, 0.0f },
            settings,
            hit));

        assert(hit.HitEntity == staticMeshWall);
        assert(hit.Fraction > 0.0f);
        assert(hit.Fraction < 1.0f);
        assert(hit.Position.x > 0.60f);
        assert(hit.Position.x < 0.66f);
        assert(hit.Normal.x < -0.99f);
    }

    // ========================================================================
    // Character Controller: front wall stop
    // ========================================================================
    {
        Scene scene;
        CreateGround(scene);
        CreateWall(scene);

        CharacterControllerConfig config{};
        config.WalkSpeed = 2.0f;
        config.RunSpeed = 2.0f;
        config.Acceleration = 100.0f;
        config.Deceleration = 100.0f;
        config.CapsuleRadius = 0.35f;
        config.CapsuleHalfLength = 0.55f;
        config.CollisionSkinWidth = 0.02f;
        CharacterController controller(config);

        TransformComponent transform{};
        transform.Position = math::Vec3{ 0.0f, 0.0f, 0.0f };

        CharacterControllerInput input{};
        input.Move.x = 1.0f;

        assert(controller.Update(input, 1.0f, scene, transform));

        // 2m進もうとしてもWall手前で停止し、CapsuleがBoxを貫通しません。
        assert(transform.Position.x > 0.60f);
        assert(transform.Position.x < 0.66f);
        assert(NearlyEqual(transform.Position.y, 0.0f));
        assert(std::fabs(controller.GetVelocity().x) < 1.0e-3f);
    }

    // ========================================================================
    // Character Controller: Static Mesh wall stop
    // ========================================================================
    // Character Controller自身も同じCapsuleCastを利用するため、Terrainの崖面をBoxへ
    // 置き換えなくても横方向の貫通を防げることを確認します。
    {
        Scene scene;
        CreateGround(scene);
        CreateStaticMeshWall(scene);

        CharacterControllerConfig config{};
        config.WalkSpeed = 2.0f;
        config.RunSpeed = 2.0f;
        config.Acceleration = 100.0f;
        config.Deceleration = 100.0f;
        config.CapsuleRadius = 0.35f;
        config.CapsuleHalfLength = 0.55f;
        config.CollisionSkinWidth = 0.02f;
        CharacterController controller(config);

        TransformComponent transform{};
        transform.Position = math::Vec3{ 0.0f, 0.0f, 0.0f };

        CharacterControllerInput input{};
        input.Move.x = 1.0f;

        assert(controller.Update(input, 1.0f, scene, transform));
        assert(transform.Position.x > 0.60f);
        assert(transform.Position.x < 0.66f);
        assert(NearlyEqual(transform.Position.y, 0.0f));
        assert(std::fabs(controller.GetVelocity().x) < 1.0e-3f);
    }

    // ========================================================================
    // Character Controller: Dynamic Body push
    // ========================================================================
    // Dynamic BoxもCharacterに対してはBlocking Hitになるため、CharacterはBox内部へ貫通しません。
    // 同時に接触点へImpulseが入り、SleepingだったDynamic Bodyが起床して+X方向へ速度を得ます。
    {
        Scene scene;
        CreateGround(scene);
        Entity dynamicBox = CreateDynamicBox(scene, 10.0f);

        CharacterControllerConfig config{};
        config.WalkSpeed = 2.0f;
        config.RunSpeed = 2.0f;
        config.Acceleration = 100.0f;
        config.Deceleration = 100.0f;
        config.CapsuleRadius = 0.35f;
        config.CapsuleHalfLength = 0.55f;
        config.CollisionSkinWidth = 0.02f;
        config.EnableDynamicBodyInteraction = true;
        config.DynamicBodyPushMass = 60.0f;
        config.DynamicBodyPushScale = 1.0f;
        config.MaxDynamicBodyPushImpulse = 120.0f;
        CharacterController controller(config);

        TransformComponent transform{};
        transform.Position = math::Vec3{ 0.0f, 0.0f, 0.0f };

        CharacterControllerInput input{};
        input.Move.x = 1.0f;

        assert(controller.Update(input, 1.0f, scene, transform));

        // Character側は従来のWallと同じ接触位置で止まり、Dynamic Bodyをすり抜けません。
        assert(transform.Position.x > 0.60f);
        assert(transform.Position.x < 0.66f);
        assert(NearlyEqual(transform.Position.y, 0.0f));

        const math::Vec3 dynamicVelocity = scene.GetPhysicsWorld().GetLinearVelocity(scene, dynamicBox);
        assert(dynamicVelocity.x > 0.0f);
        assert(std::fabs(dynamicVelocity.z) < 1.0e-3f);

        const RigidBodyComponent& rigidBody = dynamicBox.GetComponent<RigidBodyComponent>();
        assert(rigidBody.IsSleeping == false);
    }

    // ========================================================================
    // Character Controller: diagonal wall slide
    // ========================================================================
    // +X/+Zへ斜め入力した場合、+X成分だけWallに阻まれ、+Z成分は接線方向として残ります。
    {
        Scene scene;
        CreateGround(scene);
        CreateWall(scene);

        CharacterControllerConfig config{};
        config.WalkSpeed = 2.0f;
        config.RunSpeed = 2.0f;
        config.Acceleration = 100.0f;
        config.Deceleration = 100.0f;
        config.CapsuleRadius = 0.35f;
        config.CapsuleHalfLength = 0.55f;
        config.CollisionSkinWidth = 0.02f;
        CharacterController controller(config);

        TransformComponent transform{};
        transform.Position = math::Vec3{ 0.0f, 0.0f, 0.0f };

        CharacterControllerInput input{};
        input.Move = math::Vec2{ 1.0f, 1.0f };

        assert(controller.Update(input, 1.0f, scene, transform));

        assert(transform.Position.x > 0.60f);
        assert(transform.Position.x < 0.66f);
        assert(transform.Position.z > 1.0f);
        assert(std::fabs(controller.GetVelocity().x) < 1.0e-3f);
        assert(controller.GetVelocity().z > 1.0f);
    }

    // ========================================================================
    // Character Controller: low Step Up
    // ========================================================================
    // 高さ0.2mのBox段差に対してMaxStepHeight=0.3mなら、正面Capsule Castで止まらず
    // 上側Clearanceを確認して段差上面へ足元を持ち上げます。
    {
        Scene scene;
        CreateGround(scene);
        CreateLowStep(scene);

        CharacterControllerConfig config{};
        config.WalkSpeed = 0.8f;
        config.RunSpeed = 0.8f;
        config.Acceleration = 100.0f;
        config.Deceleration = 100.0f;
        config.CapsuleRadius = 0.35f;
        config.CapsuleHalfLength = 0.55f;
        config.CollisionSkinWidth = 0.02f;
        config.MaxStepHeight = 0.30f;
        CharacterController controller(config);

        TransformComponent transform{};
        transform.Position = math::Vec3{ 0.0f, 0.0f, 0.0f };

        CharacterControllerInput input{};
        input.Move.x = 1.0f;

        assert(controller.Update(input, 1.0f, scene, transform));
        assert(transform.Position.x > 0.75f);
        assert(transform.Position.x < 0.85f);
        assert(NearlyEqual(transform.Position.y, 0.20f, 2.0e-3f));
        assert(controller.IsGrounded());
    }
}

} // namespace Raven::tests
