// Raven/Character/Tests/CharacterPhysicsGroundQuerySelfTests.cpp
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

Entity CreatePlaneGround(
    Scene& scene,
    float height,
    const math::Vec3& normal)
{
    Entity ground = scene.CreateEntity("GroundQueryTestPlane");
    TransformComponent& transform = ground.GetComponent<TransformComponent>();
    transform.Position = math::Vec3{ 0.0f, height, 0.0f };

    ColliderComponent& collider = ground.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::Plane;
    collider.PlaneNormal = normal.Normalized();
    collider.Offset = math::Vec3{};
    return ground;
}

bool NearlyEqual(float a, float b, float epsilon = 1.0e-4f)
{
    return std::fabs(a - b) <= epsilon;
}

} // namespace

void RunCharacterPhysicsGroundQuerySelfTests()
{
    // ========================================================================
    // PhysicsWorld Ground Query: horizontal ground
    // ========================================================================
    {
        Scene scene;
        CreatePlaneGround(scene, 2.0f, math::Vec3{ 0.0f, 1.0f, 0.0f });

        ph::PhysicsGroundQuerySettings settings{};
        settings.MaxDistance = 5.0f;
        settings.MaxSlopeRadians = 0.872664626f;

        ph::PhysicsGroundQueryHit hit{};
        assert(scene.GetPhysicsWorld().GroundQuery(
            scene,
            math::Vec3{ 0.0f, 5.0f, 0.0f },
            settings,
            hit));
        assert(NearlyEqual(hit.Point.y, 2.0f));
        assert(NearlyEqual(hit.Distance, 3.0f));
        assert(hit.Normal.y > 0.999f);
    }

    // ========================================================================
    // PhysicsWorld Ground Query: slope rejection
    // ========================================================================
    // 60度斜面は既定50度のWalkable上限を超えるためGroundとして採用しません。
    {
        Scene scene;
        const float sixtyDegrees = 1.0471975512f;
        const math::Vec3 steepNormal{
            std::sin(sixtyDegrees),
            std::cos(sixtyDegrees),
            0.0f
        };
        CreatePlaneGround(scene, 0.0f, steepNormal);

        ph::PhysicsGroundQuerySettings settings{};
        settings.MaxDistance = 5.0f;
        settings.MaxSlopeRadians = 0.872664626f;

        ph::PhysicsGroundQueryHit hit{};
        assert(scene.GetPhysicsWorld().GroundQuery(
            scene,
            math::Vec3{ 0.0f, 2.0f, 0.0f },
            settings,
            hit) == false);
    }

    // ========================================================================
    // Character Controller: Physics ground snap
    // ========================================================================
    {
        Scene scene;
        CreatePlaneGround(scene, 1.25f, math::Vec3{ 0.0f, 1.0f, 0.0f });

        CharacterControllerConfig config{};
        config.GroundProbeStartOffset = 0.15f;
        config.GroundSnapDistance = 0.30f;
        CharacterController controller(config);

        TransformComponent transform{};
        transform.Position = math::Vec3{ 0.0f, 1.45f, 0.0f };

        CharacterControllerInput input{};
        assert(controller.Update(input, 0.0f, scene, transform));
        assert(controller.IsGrounded());
        assert(NearlyEqual(transform.Position.y, 1.25f));
        assert(controller.GetGroundNormal().y > 0.999f);
    }

    // ========================================================================
    // Character Controller: jump must not be cancelled by ground snap
    // ========================================================================
    // Jump開始Frameの前半ではGroundを検出しますが、JumpSpeedを設定した後は鉛直速度が正なので
    // Frame末尾Ground Snapを実行せず、Characterが床から離れられることを確認します。
    {
        Scene scene;
        CreatePlaneGround(scene, 0.0f, math::Vec3{ 0.0f, 1.0f, 0.0f });

        CharacterController controller{};
        TransformComponent transform{};
        transform.Position = math::Vec3{ 0.0f, 0.0f, 0.0f };

        CharacterControllerInput input{};
        input.Jump = true;

        assert(controller.Update(input, 1.0f / 60.0f, scene, transform));
        assert(controller.IsGrounded() == false);
        assert(transform.Position.y > 0.0f);
        assert(controller.GetVelocity().y > 0.0f);
    }
}

} // namespace Raven::tests
