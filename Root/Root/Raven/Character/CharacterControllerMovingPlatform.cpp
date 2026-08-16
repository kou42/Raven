// Raven/Character/CharacterControllerMovingPlatform.cpp
#include "Raven/Character/CharacterController.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{
namespace
{

bool SetMovingPlatformError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

bool IsTrackedKinematicGround(
    Scene& scene,
    Entity entity,
    TransformComponent*& outTransform)
{
    outTransform = nullptr;

    if (scene.IsEntityAlive(entity) == false)
    {
        return false;
    }

    TransformComponent* transform = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
    const RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (transform == nullptr
        || rigidBody == nullptr
        || rigidBody->Type != BodyType::Kinematic)
    {
        return false;
    }

    outTransform = transform;
    return true;
}

} // namespace

void CharacterController::ResetMovingPlatformTracking()
{
    m_MovingPlatformEntity = Entity{};
    m_MovingPlatformPosition = math::Vec3{};
    m_MovingPlatformVelocity = math::Vec3{};
    m_HasMovingPlatform = false;
}

bool CharacterController::UpdateWithMovingPlatforms(
    const CharacterControllerInput& input,
    float deltaTime,
    Scene& scene,
    TransformComponent& transform,
    float jumpPlatformHorizontalVelocityScale,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
    {
        return SetMovingPlatformError(
            errorMessage,
            "Moving Platform UpdateのdeltaTimeは0以上の有限値である必要があります");
    }
    if (std::isfinite(jumpPlatformHorizontalVelocityScale) == false
        || jumpPlatformHorizontalVelocityScale < 0.0f)
    {
        return SetMovingPlatformError(
            errorMessage,
            "Moving PlatformのJump速度継承率は0以上の有限値である必要があります");
    }

    // ========================================================================
    // Previous Ground Platform Motion
    // ========================================================================
    // 前FrameにKinematic Platformへ接地していた場合、Character自身の入力移動より先に
    // PlatformのTransform差分を適用します。
    //
    // Platform側にLinearVelocityの設定を必須とせずTransform差分を見る理由:
    // - PhysicsWorld::MovePosition()
    // - Animation
    // - Script / Gameplay Logic
    // のどの経路でKinematic Bodyを動かしても、実際にScene上で動いた量を正規データとして
    // Characterへ継承できるためです。
    bool wasOnMovingPlatform = false;
    math::Vec3 inheritedPlatformVelocity{};

    if (m_HasMovingPlatform == true)
    {
        TransformComponent* platformTransform = nullptr;
        if (IsTrackedKinematicGround(
                scene,
                m_MovingPlatformEntity,
                platformTransform) == true)
        {
            wasOnMovingPlatform = true;

            const math::Vec3 platformDisplacement =
                platformTransform->Position - m_MovingPlatformPosition;

            if (deltaTime > math::Epsilon)
            {
                inheritedPlatformVelocity = platformDisplacement / deltaTime;
            }

            m_MovingPlatformVelocity = inheritedPlatformVelocity;
            m_MovingPlatformPosition = platformTransform->Position;

            const math::Vec3 horizontalPlatformDisplacement{
                platformDisplacement.x,
                0.0f,
                platformDisplacement.z
            };

            if (horizontalPlatformDisplacement.LengthSq() > 1.0e-12f)
            {
                if (ResolvePhysicsMovement(
                        scene,
                        horizontalPlatformDisplacement,
                        transform,
                        errorMessage) == false)
                {
                    return false;
                }
            }

            if (platformDisplacement.y > 0.0f)
            {
                ph::PhysicsCapsuleCastSettings castSettings{};
                castSettings.Radius = m_Config.CapsuleRadius;
                castSettings.HalfLength = m_Config.CapsuleHalfLength;
                castSettings.SkinWidth = m_Config.CollisionSkinWidth;
                castSettings.MaxSubsteps = m_Config.MaxCapsuleCastSubsteps;
                castSettings.BinarySearchIterations = 10u;
                castSettings.IncludeStatic = true;
                castSettings.IncludeKinematic = true;
                castSettings.IncludeDynamic = false;
                castSettings.IncludePlanes = true;
                castSettings.IncludeTriggers = false;

                const math::Vec3 upwardDisplacement{ 0.0f, platformDisplacement.y, 0.0f };
                ph::PhysicsCapsuleCastHit hit{};
                if (scene.GetPhysicsWorld().CapsuleCast(
                        scene,
                        transform.Position,
                        upwardDisplacement,
                        castSettings,
                        hit) == true)
                {
                    const float safeFraction = std::clamp(
                        hit.Fraction - 1.0e-4f,
                        0.0f,
                        1.0f);
                    transform.Position += upwardDisplacement * safeFraction;
                }
                else
                {
                    transform.Position += upwardDisplacement;
                }
            }
            else if (platformDisplacement.y < 0.0f)
            {
                transform.Position.y += platformDisplacement.y;
            }
        }
        else
        {
            ResetMovingPlatformTracking();
        }
    }

    // ========================================================================
    // Incoming Dynamic Body -> Character
    // ========================================================================
    // Character自身はKinematic Bodyなので、Physics SolverだけではDynamic Bodyから押し返されません。
    // Dynamic Bodyの1Frame分の移動を相対運動へ変換し、Character Capsuleを逆向きにSweepします。
    //
    // 例: 左側の箱が+XへCharacterへ飛んでくる場合、Moving obstacleを固定した相対座標系では
    // Characterが-Xへ移動すると考えられます。Hit後に残っているFrame時間だけBody速度をCharacterへ
    // 継承し、通常のResolvePhysicsMovement()を通して押し出すことで、押された先の壁も貫通しません。
    if (m_Config.EnableDynamicBodyInteraction == true
        && deltaTime > math::Epsilon)
    {
        ph::PhysicsCapsuleCastSettings dynamicCastSettings{};
        dynamicCastSettings.Radius = m_Config.CapsuleRadius;
        dynamicCastSettings.HalfLength = m_Config.CapsuleHalfLength;
        dynamicCastSettings.SkinWidth = m_Config.CollisionSkinWidth;
        dynamicCastSettings.MaxSubsteps = m_Config.MaxCapsuleCastSubsteps;
        dynamicCastSettings.BinarySearchIterations = 10u;
        dynamicCastSettings.IncludeStatic = false;
        dynamicCastSettings.IncludeKinematic = false;
        dynamicCastSettings.IncludeDynamic = true;
        dynamicCastSettings.IncludePlanes = false;
        dynamicCastSettings.IncludeTriggers = false;

        math::Vec3 incomingDisplacement{};
        math::Vec3 incomingVelocity{};
        float bestDisplacementLengthSquared = 0.0f;

        for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
        {
            static_cast<void>(entity);

            if (rigidBody.Type != BodyType::Dynamic
                || rigidBody.InverseMass <= 0.0f)
            {
                continue;
            }

            const math::Vec3 bodyHorizontalVelocity{
                rigidBody.LinearVelocity.x,
                0.0f,
                rigidBody.LinearVelocity.z
            };
            const math::Vec3 bodyDisplacement = bodyHorizontalVelocity * deltaTime;
            if (bodyDisplacement.LengthSq() <= 1.0e-12f)
            {
                continue;
            }

            const math::Vec3 relativeCharacterDisplacement = -bodyDisplacement;
            ph::PhysicsCapsuleCastHit dynamicHit{};
            if (scene.GetPhysicsWorld().CapsuleCast(
                    scene,
                    transform.Position,
                    relativeCharacterDisplacement,
                    dynamicCastSettings,
                    dynamicHit) == false)
            {
                continue;
            }

            const RigidBodyComponent* hitRigidBody =
                scene.TryGetComponent<RigidBodyComponent>(dynamicHit.HitEntity.GetIndex());
            if (hitRigidBody == nullptr
                || hitRigidBody->Type != BodyType::Dynamic
                || hitRigidBody->InverseMass <= 0.0f)
            {
                continue;
            }

            const math::Vec3 hitBodyHorizontalVelocity{
                hitRigidBody->LinearVelocity.x,
                0.0f,
                hitRigidBody->LinearVelocity.z
            };

            // Fraction=0なら既に接触しているため1Frame分、Fraction=1ならFrame末尾接触なので0です。
            const float remainingTimeFraction = std::clamp(
                1.0f - dynamicHit.Fraction,
                0.0f,
                1.0f);
            const math::Vec3 candidateDisplacement =
                hitBodyHorizontalVelocity * deltaTime * remainingTimeFraction;
            const float candidateLengthSquared = candidateDisplacement.LengthSq();
            if (candidateLengthSquared <= bestDisplacementLengthSquared)
            {
                continue;
            }

            bestDisplacementLengthSquared = candidateLengthSquared;
            incomingDisplacement = candidateDisplacement;
            incomingVelocity = hitBodyHorizontalVelocity;
        }

        if (bestDisplacementLengthSquared > 1.0e-12f)
        {
            // PositionだけでなくController速度へも反映し、次Frameに押し返しが即消失するのを防ぎます。
            m_Velocity.x = incomingVelocity.x;
            m_Velocity.z = incomingVelocity.z;

            if (ResolvePhysicsMovement(
                    scene,
                    incomingDisplacement,
                    transform,
                    errorMessage) == false)
            {
                return false;
            }
        }
    }

    const bool wasGrounded = m_Grounded;

    if (UpdateInternal(
            input,
            deltaTime,
            &scene,
            transform,
            errorMessage) == false)
    {
        return false;
    }

    // ========================================================================
    // Jump Velocity Inheritance
    // ========================================================================
    if (input.Jump == true
        && wasGrounded == true
        && wasOnMovingPlatform == true
        && m_Grounded == false
        && jumpPlatformHorizontalVelocityScale > 0.0f)
    {
        const math::Vec3 inheritedHorizontalVelocity{
            inheritedPlatformVelocity.x * jumpPlatformHorizontalVelocityScale,
            0.0f,
            inheritedPlatformVelocity.z * jumpPlatformHorizontalVelocityScale
        };

        m_Velocity.x += inheritedHorizontalVelocity.x;
        m_Velocity.z += inheritedHorizontalVelocity.z;

        const math::Vec3 inheritedDisplacement = inheritedHorizontalVelocity * deltaTime;
        if (inheritedDisplacement.LengthSq() > 1.0e-12f)
        {
            if (ResolvePhysicsMovement(
                    scene,
                    inheritedDisplacement,
                    transform,
                    errorMessage) == false)
            {
                return false;
            }
        }
    }

    // ========================================================================
    // Capture Current Ground Platform
    // ========================================================================
    if (m_Grounded == true)
    {
        ph::PhysicsGroundQuerySettings groundSettings{};
        groundSettings.MaxDistance = m_Config.GroundProbeStartOffset + m_Config.GroundSnapDistance;
        groundSettings.MaxSlopeRadians = m_Config.MaxGroundSlopeRadians;
        groundSettings.IncludeStatic = true;
        groundSettings.IncludeKinematic = true;
        groundSettings.IncludeDynamic = false;
        groundSettings.IncludePlanes = true;

        const math::Vec3 probeOrigin = transform.Position
            + math::Vec3{ 0.0f, m_Config.GroundProbeStartOffset, 0.0f };

        ph::PhysicsGroundQueryHit groundHit{};
        if (scene.GetPhysicsWorld().GroundQuery(
                scene,
                probeOrigin,
                groundSettings,
                groundHit) == true)
        {
            TransformComponent* platformTransform = nullptr;
            if (IsTrackedKinematicGround(
                    scene,
                    groundHit.HitEntity,
                    platformTransform) == true)
            {
                const bool samePlatform = m_HasMovingPlatform == true
                    && m_MovingPlatformEntity == groundHit.HitEntity;

                m_MovingPlatformEntity = groundHit.HitEntity;
                m_MovingPlatformPosition = platformTransform->Position;
                m_HasMovingPlatform = true;

                if (samePlatform == false)
                {
                    m_MovingPlatformVelocity = math::Vec3{};
                }

                return true;
            }
        }
    }

    ResetMovingPlatformTracking();
    return true;
}

} // namespace Raven