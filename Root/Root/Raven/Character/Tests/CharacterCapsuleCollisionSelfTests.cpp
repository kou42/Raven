// Raven/Character/Tests/CharacterCapsuleCollisionSelfTests.cpp
#include <cassert>
#include <cmath>

#include "Raven/Character/CharacterController.h"
#include "Raven/Physics/PhysicsWorld.h"
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

bool NearlyEqual(float a, float b, float epsilon = 1.0e-3f)
{
    return std::fabs(a - b) <= epsilon;
}

} // namespace

void RunCharacterCapsuleCollisionSelfTests()
{
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
}

} // namespace Raven::tests
