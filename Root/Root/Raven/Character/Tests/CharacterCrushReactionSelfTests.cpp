// Raven/Character/Tests/CharacterCrushReactionSelfTests.cpp
#include <cassert>
#include <cmath>

#include "Raven/Character/CharacterController.h"
#include "Raven/Character/CharacterCrushReaction.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::tests
{
namespace
{

Entity CreateGround(Scene& scene)
{
    Entity ground = scene.CreateEntity("CharacterCrushReactionGround");
    ColliderComponent& collider = ground.AddComponent<ColliderComponent>();
    collider.Type = ColliderType::Plane;
    collider.PlaneNormal = math::Vec3{ 0.0f, 1.0f, 0.0f };
    collider.IsTrigger = false;
    return ground;
}

Entity CreateCrushBox(
    Scene& scene,
    const math::Vec3& position,
    const math::Vec3& velocity,
    const char* name)
{
    Entity box = scene.CreateEntity(name);
    box.GetComponent<TransformComponent>().Position = position;

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

bool NearlyEqual(float lhs, float rhs, float epsilon = 1.0e-4f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

void RunCharacterCrushReactionSelfTests()
{
    CharacterCrushReactionConfig reactionConfig{};
    reactionConfig.DamageDelay = 0.25f;
    reactionConfig.RagdollDurationThreshold = 0.75f;
    reactionConfig.RagdollExposureThreshold = 2.0f;

    // ========================================================================
    // No Crush -> No Reaction
    // ========================================================================
    {
        CharacterController controller{};
        CharacterCrushReactionState reaction{};
        assert(EvaluateCharacterCrushReaction(controller, reactionConfig, reaction));
        assert(reaction.ShouldApplyDamage == false);
        assert(reaction.ShouldEnterRagdoll == false);
        assert(NearlyEqual(reaction.Duration, 0.0f));
        assert(NearlyEqual(reaction.Exposure, 0.0f));
    }

    // ========================================================================
    // Continuous Crush -> Damage -> Ragdoll
    // ========================================================================
    // 左右のDynamic Bodyを接触直前に置き、0.25秒刻みで継続Crushを作ります。
    // 1Frame目でDamage条件へ到達し、3Frame目でDuration=0.75秒となってRagdoll要求へ進みます。
    {
        Scene scene;
        CreateGround(scene);
        CreateCrushBox(scene, math::Vec3{ -0.60f, 0.50f, 0.0f }, math::Vec3{ 1.0f, 0.0f, 0.0f }, "CharacterCrushReactionLeft");
        CreateCrushBox(scene, math::Vec3{ 0.60f, 0.50f, 0.0f }, math::Vec3{ -1.0f, 0.0f, 0.0f }, "CharacterCrushReactionRight");

        CharacterController controller{};
        TransformComponent transform{};
        CharacterControllerInput input{};
        CharacterCrushReactionState reaction{};

        assert(controller.UpdateWithMovingPlatforms(input, 0.25f, scene, transform));
        assert(EvaluateCharacterCrushReaction(controller, reactionConfig, reaction));
        assert(reaction.ShouldApplyDamage == true);
        assert(reaction.ShouldEnterRagdoll == false);
        assert(NearlyEqual(reaction.Duration, 0.25f));

        assert(controller.UpdateWithMovingPlatforms(input, 0.25f, scene, transform));
        assert(EvaluateCharacterCrushReaction(controller, reactionConfig, reaction));
        assert(reaction.ShouldApplyDamage == true);
        assert(reaction.ShouldEnterRagdoll == false);
        assert(NearlyEqual(reaction.Duration, 0.50f));

        assert(controller.UpdateWithMovingPlatforms(input, 0.25f, scene, transform));
        assert(EvaluateCharacterCrushReaction(controller, reactionConfig, reaction));
        assert(reaction.ShouldApplyDamage == true);
        assert(reaction.ShouldEnterRagdoll == true);
        assert(NearlyEqual(reaction.Duration, 0.75f));
    }

    // ========================================================================
    // Exposure can trigger Ragdoll earlier than Duration
    // ========================================================================
    // Duration条件を長くし、Exposure閾値だけを低くして、強いCrushを時間とは独立に扱えることを確認します。
    {
        Scene scene;
        CreateGround(scene);
        CreateCrushBox(scene, math::Vec3{ -0.60f, 0.50f, 0.0f }, math::Vec3{ 4.0f, 0.0f, 0.0f }, "CharacterCrushExposureLeft");
        CreateCrushBox(scene, math::Vec3{ 0.60f, 0.50f, 0.0f }, math::Vec3{ -4.0f, 0.0f, 0.0f }, "CharacterCrushExposureRight");

        CharacterController controller{};
        TransformComponent transform{};
        CharacterControllerInput input{};
        assert(controller.UpdateWithMovingPlatforms(input, 0.25f, scene, transform));

        CharacterCrushReactionConfig exposureConfig{};
        exposureConfig.DamageDelay = 0.0f;
        exposureConfig.RagdollDurationThreshold = 10.0f;
        exposureConfig.RagdollExposureThreshold = 0.5f;

        CharacterCrushReactionState reaction{};
        assert(EvaluateCharacterCrushReaction(controller, exposureConfig, reaction));
        assert(reaction.ShouldApplyDamage == true);
        assert(reaction.ShouldEnterRagdoll == true);
        assert(reaction.Exposure >= 0.5f);
    }

    // ========================================================================
    // Disabled Ragdoll thresholds
    // ========================================================================
    // Duration / Exposureの両方を0以下にすると、Damageだけを利用するゲームにも対応できます。
    {
        Scene scene;
        CreateGround(scene);
        CreateCrushBox(scene, math::Vec3{ -0.60f, 0.50f, 0.0f }, math::Vec3{ 1.0f, 0.0f, 0.0f }, "CharacterCrushNoRagdollLeft");
        CreateCrushBox(scene, math::Vec3{ 0.60f, 0.50f, 0.0f }, math::Vec3{ -1.0f, 0.0f, 0.0f }, "CharacterCrushNoRagdollRight");

        CharacterController controller{};
        TransformComponent transform{};
        CharacterControllerInput input{};
        assert(controller.UpdateWithMovingPlatforms(input, 1.0f, scene, transform));

        CharacterCrushReactionConfig damageOnlyConfig{};
        damageOnlyConfig.DamageDelay = 0.0f;
        damageOnlyConfig.RagdollDurationThreshold = 0.0f;
        damageOnlyConfig.RagdollExposureThreshold = 0.0f;

        CharacterCrushReactionState reaction{};
        assert(EvaluateCharacterCrushReaction(controller, damageOnlyConfig, reaction));
        assert(reaction.ShouldApplyDamage == true);
        assert(reaction.ShouldEnterRagdoll == false);
    }
}

} // namespace Raven::tests
