// Raven/Character/Tests/CharacterCeilingCollisionSelfTests.cpp
#include "Raven/Character/Tests/CharacterCeilingCollisionSelfTests.h"

#include <cassert>
#include <cmath>

#include "Raven/Character/CharacterController.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::tests
{
namespace
{

Entity CreateGround(Scene& scene)
{
    Entity ground = scene.CreateEntity("CharacterCeilingTestGround");
    ColliderComponent& collider = ground.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::Plane;
    collider.PlaneNormal = math::Vec3{ 0.0f, 1.0f, 0.0f };
    collider.IsTrigger = false;
    return ground;
}

Entity CreateCeiling(Scene& scene, float bottomHeight)
{
    Entity ceiling = scene.CreateEntity("CharacterCeilingTestBox");
    TransformComponent& transform = ceiling.GetComponent<TransformComponent>();
    transform.Position = math::Vec3{ 0.0f, bottomHeight + 0.10f, 0.0f };

    ColliderComponent& collider = ceiling.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::Box;
    collider.HalfExtents = math::Vec3{ 2.0f, 0.10f, 2.0f };
    collider.IsTrigger = false;
    return ceiling;
}

bool NearlyEqual(float a, float b, float tolerance = 1.0e-4f)
{
    return std::fabs(a - b) <= tolerance;
}

} // namespace

void RunCharacterCeilingCollisionSelfTests()
{
    // ========================================================================
    // Jump -> Ceiling Capsule Cast
    // ========================================================================
    // Characterの実Capsule全高は1.8m、SkinWidth込みCast半径を考慮すると約1.82mです。
    // Ceiling下面を2.4mへ置いた場合、足元Rootはおよそ0.58mで上昇を止める必要があります。
    // 大きなdeltaTimeでも最終位置だけをClampするのではなくSweepすることで天井を貫通しません。
    {
        Scene scene;
        CreateGround(scene);
        CreateCeiling(scene, 2.40f);

        CharacterControllerConfig config{};
        config.JumpSpeed = 6.0f;
        config.Gravity = -9.81f;
        config.CapsuleRadius = 0.35f;
        config.CapsuleHalfLength = 0.55f;
        config.CollisionSkinWidth = 0.02f;
        config.GroundSnapDistance = 0.20f;
        CharacterController controller(config);

        TransformComponent transform{};
        transform.Position = math::Vec3{ 0.0f, 0.0f, 0.0f };

        CharacterControllerInput input{};
        input.Jump = true;

        // 0.2秒なら本来1.2m上昇しようとしますが、天井手前で停止する必要があります。
        assert(controller.Update(input, 0.20f, scene, transform));
        assert(transform.Position.y > 0.50f);
        assert(transform.Position.y < 0.62f);
        assert(std::fabs(controller.GetVelocity().y) < 1.0e-4f);
        assert(controller.IsGrounded() == false);
    }

    // ========================================================================
    // No ceiling -> normal jump
    // ========================================================================
    // 天井が無い場合は従来通りJumpSpeed分だけ上昇し、Capsule Cast追加が通常ジャンプを
    // 不必要に止めないことを確認します。
    {
        Scene scene;
        CreateGround(scene);

        CharacterControllerConfig config{};
        config.JumpSpeed = 4.0f;
        CharacterController controller(config);

        TransformComponent transform{};
        transform.Position = math::Vec3{ 0.0f, 0.0f, 0.0f };

        CharacterControllerInput input{};
        input.Jump = true;

        assert(controller.Update(input, 0.10f, scene, transform));
        assert(transform.Position.y > 0.35f);
        assert(controller.GetVelocity().y > 0.0f);
        assert(controller.IsGrounded() == false);
    }

    // ========================================================================
    // Walk / Run / Sprint speed priority
    // ========================================================================
    // Sprint追加後も通常移動とRunの既存意味を変えず、Sprint要求時だけ最高速へ昇格することを固定します。
    // 各CaseでControllerを作り直し、前CaseのVelocityや加速履歴が判定へ影響しないようにします。
    {
        CharacterControllerConfig config{};
        config.WalkSpeed = 2.0f;
        config.RunSpeed = 4.0f;
        config.SprintSpeed = 7.0f;
        config.Acceleration = 100.0f;
        config.Deceleration = 100.0f;

        CharacterControllerInput input{};
        input.Move = math::Vec2{ 0.0f, 1.0f };

        TransformComponent walkTransform{};
        CharacterController walkController(config);
        assert(walkController.Update(input, 0.10f, walkTransform));
        assert(NearlyEqual(walkController.GetHorizontalSpeed(), config.WalkSpeed));

        TransformComponent runTransform{};
        CharacterController runController(config);
        input.Run = true;
        assert(runController.Update(input, 0.10f, runTransform));
        assert(NearlyEqual(runController.GetHorizontalSpeed(), config.RunSpeed));

        TransformComponent sprintTransform{};
        CharacterController sprintController(config);
        input.Run = false;
        input.Sprint = true;
        assert(sprintController.Update(input, 0.10f, sprintTransform));
        assert(NearlyEqual(sprintController.GetHorizontalSpeed(), config.SprintSpeed));

        // KeyboardとGamepadを同時に使う場合などRun/Sprintが両方trueでも、Sprintを優先する規約を固定します。
        TransformComponent combinedTransform{};
        CharacterController combinedController(config);
        input.Run = true;
        input.Sprint = true;
        assert(combinedController.Update(input, 0.10f, combinedTransform));
        assert(NearlyEqual(combinedController.GetHorizontalSpeed(), config.SprintSpeed));
    }

    // SprintSpeedがRunSpeed未満のConfigは4段階速度規約に反するため、更新開始前に拒否します。
    {
        CharacterControllerConfig invalidConfig{};
        invalidConfig.WalkSpeed = 2.0f;
        invalidConfig.RunSpeed = 5.0f;
        invalidConfig.SprintSpeed = 4.0f;
        CharacterController controller(invalidConfig);

        TransformComponent transform{};
        CharacterControllerInput input{};
        input.Move = math::Vec2{ 0.0f, 1.0f };
        input.Sprint = true;

        std::string errorMessage;
        assert(controller.Update(input, 0.10f, transform, &errorMessage) == false);
        assert(errorMessage.empty() == false);
    }
}

} // namespace Raven::tests
