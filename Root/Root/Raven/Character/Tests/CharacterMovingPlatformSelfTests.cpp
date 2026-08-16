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

Entity CreateGround(Scene& scene)
{
    Entity ground = scene.CreateEntity("CharacterDynamicResponseGround");
    ColliderComponent& collider = ground.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::Plane;
    collider.PlaneNormal = math::Vec3{ 0.0f, 1.0f, 0.0f };
    collider.IsTrigger = false;
    return ground;
}

Entity CreateIncomingDynamicBox(
    Scene& scene,
    const math::Vec3& position,
    const math::Vec3& velocity,
    const char* name)
{
    Entity box = scene.CreateEntity(name);

    TransformComponent& transform = box.GetComponent<TransformComponent>();
    transform.Position = position;

    ColliderComponent& collider = box.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::Box;
    collider.HalfExtents = math::Vec3{ 0.25f, 0.50f, 0.50f };
    collider.IsTrigger = false;

    RigidBodyComponent& rigidBody = box.AddComponent<RigidBodyComponent>();
    rigidBody.SetBodyType(BodyType::Dynamic);
    rigidBody.SetMass(10.0f);
    rigidBody.UseGravity = false;
    rigidBody.LinearVelocity = velocity;
    return box;
}

Entity CreateBlockingWall(Scene& scene)
{
    Entity wall = scene.CreateEntity("CharacterDynamicResponseWall");
    TransformComponent& transform = wall.GetComponent<TransformComponent>();
    transform.Position = math::Vec3{ 0.75f, 1.0f, 0.0f };

    ColliderComponent& collider = wall.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::Box;
    collider.HalfExtents = math::Vec3{ 0.25f, 1.0f, 1.0f };
    collider.IsTrigger = false;
    return wall;
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
    // Dynamic Body -> Character response
    // ========================================================================
    // Characterが入力していなくても、1Frame以内に到達するDynamic Bodyの水平速度を相対Sweepで検出し、
    // Hit後の残り時間分だけCharacterがBody進行方向へ押し出されることを確認します。
    {
        Scene scene;
        CreateGround(scene);
        CreateIncomingDynamicBox(
            scene,
            math::Vec3{ -1.25f, 0.50f, 0.0f },
            math::Vec3{ 1.0f, 0.0f, 0.0f },
            "CharacterIncomingDynamicBox");

        CharacterControllerConfig config{};
        config.Acceleration = 100.0f;
        config.Deceleration = 100.0f;
        config.EnableDynamicBodyInteraction = true;
        CharacterController controller(config);

        TransformComponent characterTransform{};
        characterTransform.Position = math::Vec3{ 0.0f, 0.0f, 0.0f };
        CharacterControllerInput input{};

        assert(controller.UpdateWithMovingPlatforms(input, 1.0f, scene, characterTransform));

        // Boxは左から+Xへ近付くため、Characterも+Xへ押し出されます。
        // Character自身はDynamic化せず、ControllerのKinematic移動経路で応答します。
        assert(characterTransform.Position.x > 0.25f);
        assert(characterTransform.Position.x < 0.45f);
        assert(NearlyEqual(characterTransform.Position.y, 0.0f));
    }

    // ========================================================================
    // Multiple Dynamic Bodies: same direction must not double speed
    // ========================================================================
    // 2個の箱が同じ方向・同じ速度で押しても、平均統合によりCharacter変位が単純に2倍にならないことを確認します。
    {
        Scene scene;
        CreateGround(scene);
        CreateIncomingDynamicBox(scene, math::Vec3{ -1.25f, 0.50f, -0.20f }, math::Vec3{ 1.0f, 0.0f, 0.0f }, "CharacterIncomingDynamicBoxA");
        CreateIncomingDynamicBox(scene, math::Vec3{ -1.25f, 0.50f, 0.20f }, math::Vec3{ 1.0f, 0.0f, 0.0f }, "CharacterIncomingDynamicBoxB");

        CharacterController controller{};
        TransformComponent characterTransform{};
        CharacterControllerInput input{};
        assert(controller.UpdateWithMovingPlatforms(input, 1.0f, scene, characterTransform));
        assert(characterTransform.Position.x > 0.20f);
        assert(characterTransform.Position.x < 0.50f);
    }

    // ========================================================================
    // Multiple Dynamic Bodies: opposite directions cancel
    // ========================================================================
    // 左右から同程度に押された場合、合成変位がほぼ0になりCharacterが一方向へ吹き飛ばされないことを確認します。
    {
        Scene scene;
        CreateGround(scene);
        CreateIncomingDynamicBox(scene, math::Vec3{ -1.25f, 0.50f, 0.0f }, math::Vec3{ 1.0f, 0.0f, 0.0f }, "CharacterIncomingDynamicBoxLeft");
        CreateIncomingDynamicBox(scene, math::Vec3{ 1.25f, 0.50f, 0.0f }, math::Vec3{ -1.0f, 0.0f, 0.0f }, "CharacterIncomingDynamicBoxRight");

        CharacterController controller{};
        TransformComponent characterTransform{};
        CharacterControllerInput input{};
        assert(controller.UpdateWithMovingPlatforms(input, 1.0f, scene, characterTransform));
        assert(std::fabs(characterTransform.Position.x) < 0.10f);
    }

    // ========================================================================
    // Dynamic Body pushes Character toward static wall
    // ========================================================================
    // 押し返し変位もResolvePhysicsMovement()を通るため、Characterが壁内部へ押し込まれないことを確認します。
    {
        Scene scene;
        CreateGround(scene);
        CreateBlockingWall(scene);
        CreateIncomingDynamicBox(scene, math::Vec3{ -1.25f, 0.50f, 0.0f }, math::Vec3{ 1.0f, 0.0f, 0.0f }, "CharacterIncomingDynamicBoxWallCase");

        CharacterController controller{};
        TransformComponent characterTransform{};
        CharacterControllerInput input{};
        assert(controller.UpdateWithMovingPlatforms(input, 1.0f, scene, characterTransform));

        // Wall左面x=0.5、Character Capsule Radius+Skinがおよそ0.37なので、足元中心は約0.13より先へ進みません。
        assert(characterTransform.Position.x >= 0.0f);
        assert(characterTransform.Position.x < 0.16f);
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

        assert(controller.UpdateWithMovingPlatforms(input, 1.0f / 60.0f, scene, characterTransform));
        assert(controller.IsOnMovingPlatform());

        scene.DestroyEntity(platform);

        assert(controller.UpdateWithMovingPlatforms(input, 1.0f / 60.0f, scene, characterTransform));
        assert(controller.IsOnMovingPlatform() == false);
    }
}

} // namespace Raven::tests