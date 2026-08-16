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
        assert(controller.IsCrushed() == false);
        assert(NearlyEqual(controller.GetCrushDuration(), 0.0f));
        assert(NearlyEqual(controller.GetCrushExposure(), 0.0f));
    }

    // ========================================================================
    // Multiple Dynamic Bodies: same direction must not double speed
    // ========================================================================
    // 2個の箱が同じ方向・同じ速度で押しても、平均統合によりCharacter変位が単純に2倍にならないことを確認します。
    // 同方向の押しは逃げ道があるためCrushにもなりません。
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
        assert(controller.IsCrushed() == false);
    }

    // ========================================================================
    // Crush Detection: opposite Dynamic Bodies
    // ========================================================================
    // 左右から同程度の速度で押された場合、Characterの合成変位はほぼ0でも「外力が無い」のではありません。
    // 各Bodyの押し速度が相殺しているためCrushとして検出し、代表速度も保持することを確認します。
    {
        Scene scene;
        CreateGround(scene);
        CreateIncomingDynamicBox(scene, math::Vec3{ -1.25f, 0.50f, 0.0f }, math::Vec3{ 1.0f, 0.0f, 0.0f }, "CharacterIncomingDynamicBoxLeft");
        CreateIncomingDynamicBox(scene, math::Vec3{ 1.25f, 0.50f, 0.0f }, math::Vec3{ -1.0f, 0.0f, 0.0f }, "CharacterIncomingDynamicBoxRight");

        CharacterController controller{};
        TransformComponent characterTransform{};
        CharacterControllerInput input{};

        assert(controller.UpdateWithMovingPlatforms(input, 0.25f, scene, characterTransform));
        assert(std::fabs(characterTransform.Position.x) < 0.10f);
        assert(controller.IsCrushed());
        assert(controller.GetCrushStrength() >= 0.15f);
        assert(NearlyEqual(controller.GetCrushDuration(), 0.25f));
        const float firstExposure = controller.GetCrushExposure();
        assert(firstExposure > 0.0f);

        // 同じ圧力が途切れず続くと、瞬間IsCrushedだけでなくDuration / ExposureもFrameを跨いで増加します。
        assert(controller.UpdateWithMovingPlatforms(input, 0.25f, scene, characterTransform));
        assert(controller.IsCrushed());
        assert(NearlyEqual(controller.GetCrushDuration(), 0.50f));
        assert(controller.GetCrushExposure() > firstExposure);
    }

    // ========================================================================
    // Crush Detection: Dynamic Body -> Character -> Static Wall
    // ========================================================================
    // Dynamic Bodyの要求変位に対して壁が逃げ道を塞ぎ、Characterがほとんど移動できない場合をCrushとします。
    // 押し返し自体はResolvePhysicsMovement()を通るため、Crushを検出しても壁内部へ貫通させません。
    {
        Scene scene;
        CreateGround(scene);
        CreateBlockingWall(scene);
        CreateIncomingDynamicBox(scene, math::Vec3{ -1.25f, 0.50f, 0.0f }, math::Vec3{ 1.0f, 0.0f, 0.0f }, "CharacterIncomingDynamicBoxWallCase");

        CharacterController controller{};
        TransformComponent characterTransform{};
        CharacterControllerInput input{};
        assert(controller.UpdateWithMovingPlatforms(input, 1.0f, scene, characterTransform));

        // Wall左面x=0.37はCharacter Capsule Radius+Skinとほぼ一致するため、+Xへ逃げる余地がありません。
        assert(characterTransform.Position.x < 0.03f);
        assert(controller.IsCrushed());
        assert(controller.GetCrushStrength() >= 0.15f);
        assert(NearlyEqual(controller.GetCrushDuration(), 1.0f));
        assert(controller.GetCrushExposure() >= controller.GetCrushStrength() - 1.0e-3f);
    }

    // ========================================================================
    // Crush state resets when pressure disappears
    // ========================================================================
    // CrushはLatch状態ではなく現在Frameの接触状態です。押していたBodyが停止した次Frameには解除され、
    // 連続時間とExposureも同時に0へ戻ります。
    {
        Scene scene;
        CreateGround(scene);
        Entity left = CreateIncomingDynamicBox(scene, math::Vec3{ -1.25f, 0.50f, 0.0f }, math::Vec3{ 1.0f, 0.0f, 0.0f }, "CharacterCrushResetLeft");
        Entity right = CreateIncomingDynamicBox(scene, math::Vec3{ 1.25f, 0.50f, 0.0f }, math::Vec3{ -1.0f, 0.0f, 0.0f }, "CharacterCrushResetRight");

        CharacterController controller{};
        TransformComponent characterTransform{};
        CharacterControllerInput input{};
        assert(controller.UpdateWithMovingPlatforms(input, 0.5f, scene, characterTransform));
        assert(controller.IsCrushed());
        assert(controller.GetCrushDuration() > 0.0f);
        assert(controller.GetCrushExposure() > 0.0f);

        left.GetComponent<RigidBodyComponent>().LinearVelocity = math::Vec3{};
        right.GetComponent<RigidBodyComponent>().LinearVelocity = math::Vec3{};
        assert(controller.UpdateWithMovingPlatforms(input, 1.0f / 60.0f, scene, characterTransform));
        assert(controller.IsCrushed() == false);
        assert(NearlyEqual(controller.GetCrushStrength(), 0.0f));
        assert(NearlyEqual(controller.GetCrushDuration(), 0.0f));
        assert(NearlyEqual(controller.GetCrushExposure(), 0.0f));
    }

    // ========================================================================
    // Explicit Crush tracking reset
    // ========================================================================
    // Teleport / Scene切替 / Ragdoll切替などでは、Dynamic Bodyがまだ近くにいても以前のExposureを
    // 新しい状態へ持ち越してはいけません。ResetCrushTracking()で全Crush履歴を破棄できることを確認します。
    {
        Scene scene;
        CreateGround(scene);
        CreateIncomingDynamicBox(scene, math::Vec3{ -1.25f, 0.50f, 0.0f }, math::Vec3{ 1.0f, 0.0f, 0.0f }, "CharacterCrushExplicitResetLeft");
        CreateIncomingDynamicBox(scene, math::Vec3{ 1.25f, 0.50f, 0.0f }, math::Vec3{ -1.0f, 0.0f, 0.0f }, "CharacterCrushExplicitResetRight");

        CharacterController controller{};
        TransformComponent characterTransform{};
        CharacterControllerInput input{};
        assert(controller.UpdateWithMovingPlatforms(input, 0.5f, scene, characterTransform));
        assert(controller.IsCrushed());

        controller.ResetCrushTracking();
        assert(controller.IsCrushed() == false);
        assert(NearlyEqual(controller.GetCrushStrength(), 0.0f));
        assert(NearlyEqual(controller.GetCrushDuration(), 0.0f));
        assert(NearlyEqual(controller.GetCrushExposure(), 0.0f));
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