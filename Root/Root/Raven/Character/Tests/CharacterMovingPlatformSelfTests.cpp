// Raven/Character/Tests/CharacterMovingPlatformSelfTests.cpp
#include <cassert>
#include <cmath>

#include "Raven/Character/CharacterController.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::tests
{
namespace
{

Entity CreateMovingPlatform(Scene& scene)
{
    Entity platform = scene.CreateEntity("CharacterMovingPlatformTest");

    TransformComponent& transform = platform.GetComponent<TransformComponent>();
    transform.Position = math::Vec3{ 0.0f, -0.10f, 0.0f };

    ColliderComponent& collider = platform.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::Box;
    collider.HalfExtents = math::Vec3{ 2.0f, 0.10f, 2.0f };
    collider.IsTrigger = false;

    RigidBodyComponent& rigidBody = platform.AddComponent<RigidBodyComponent>();
    rigidBody.SetBodyType(BodyType::Kinematic);
    rigidBody.UseGravity = false;
    return platform;
}

bool NearlyEqual(float a, float b, float epsilon = 2.0e-3f)
{
    return std::fabs(a - b) <= epsilon;
}

} // namespace

void RunCharacterMovingPlatformSelfTests()
{
    // ========================================================================
    // Horizontal Platform Carry
    // ========================================================================
    {
        Scene scene;
        Entity platform = CreateMovingPlatform(scene);

        CharacterController controller{};
        TransformComponent characterTransform{};
        characterTransform.Position = math::Vec3{ 0.0f, 0.0f, 0.0f };
        CharacterControllerInput input{};

        // 最初のFrameでKinematic Groundを捕捉します。
        assert(controller.UpdateWithMovingPlatforms(
            input,
            1.0f / 60.0f,
            scene,
            characterTransform));
        assert(controller.IsGrounded());
        assert(controller.IsOnMovingPlatform());
        assert(controller.GetMovingPlatformEntity() == platform);

        // Platformを+Xへ0.5m移動すると、次FrameでCharacterも同量搬送されます。
        platform.GetComponent<TransformComponent>().Position.x += 0.5f;

        assert(controller.UpdateWithMovingPlatforms(
            input,
            0.5f,
            scene,
            characterTransform));

        assert(NearlyEqual(characterTransform.Position.x, 0.5f));
        assert(NearlyEqual(controller.GetMovingPlatformVelocity().x, 1.0f));
        assert(controller.IsGrounded());
    }

    // ========================================================================
    // Vertical Platform Carry
    // ========================================================================
    {
        Scene scene;
        Entity platform = CreateMovingPlatform(scene);

        CharacterController controller{};
        TransformComponent characterTransform{};
        characterTransform.Position = math::Vec3{ 0.0f, 0.0f, 0.0f };
        CharacterControllerInput input{};

        assert(controller.UpdateWithMovingPlatforms(
            input,
            1.0f / 60.0f,
            scene,
            characterTransform));

        // Platform上面を0.2m持ち上げます。Character足元も同じ高さへ追従します。
        platform.GetComponent<TransformComponent>().Position.y += 0.2f;

        assert(controller.UpdateWithMovingPlatforms(
            input,
            0.2f,
            scene,
            characterTransform));

        assert(NearlyEqual(characterTransform.Position.y, 0.2f));
        assert(NearlyEqual(controller.GetMovingPlatformVelocity().y, 1.0f));
        assert(controller.IsGrounded());
    }

    // ========================================================================
    // Jump Horizontal Velocity Inheritance
    // ========================================================================
    {
        Scene scene;
        Entity platform = CreateMovingPlatform(scene);

        CharacterController controller{};
        TransformComponent characterTransform{};
        characterTransform.Position = math::Vec3{ 0.0f, 0.0f, 0.0f };
        CharacterControllerInput input{};

        assert(controller.UpdateWithMovingPlatforms(
            input,
            1.0f / 60.0f,
            scene,
            characterTransform));

        platform.GetComponent<TransformComponent>().Position.x += 0.5f;
        input.Jump = true;

        assert(controller.UpdateWithMovingPlatforms(
            input,
            0.5f,
            scene,
            characterTransform,
            1.0f));

        assert(controller.IsGrounded() == false);
        assert(controller.IsOnMovingPlatform() == false);
        assert(controller.GetVelocity().x > 0.95f);
        assert(controller.GetVelocity().y > 0.0f);
    }

    // ========================================================================
    // Tracking Reset after Platform destruction
    // ========================================================================
    {
        Scene scene;
        Entity platform = CreateMovingPlatform(scene);

        CharacterController controller{};
        TransformComponent characterTransform{};
        CharacterControllerInput input{};

        assert(controller.UpdateWithMovingPlatforms(
            input,
            1.0f / 60.0f,
            scene,
            characterTransform));
        assert(controller.IsOnMovingPlatform());

        scene.DestroyEntity(platform);

        assert(controller.UpdateWithMovingPlatforms(
            input,
            1.0f / 60.0f,
            scene,
            characterTransform));
        assert(controller.IsOnMovingPlatform() == false);
    }
}

} // namespace Raven::tests