// Raven/Character/Tests/CharacterCeilingCollisionSelfTests.cpp
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
}

} // namespace Raven::tests
