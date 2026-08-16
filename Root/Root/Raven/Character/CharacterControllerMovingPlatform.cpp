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

            // ---------------------------------------------------------------
            // Horizontal Carry
            // ---------------------------------------------------------------
            // PlatformがCharacterを壁へ押す場合も通常のCharacter入力と同じCapsule Cast / Slideを通します。
            // これによりMoving Platformによる壁貫通を防ぎます。
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

            // ---------------------------------------------------------------
            // Vertical Carry
            // ---------------------------------------------------------------
            // 上昇Platformでは天井への押し潰し/貫通を避けるためCapsule Castします。
            // 下降Platformはこの後の通常UpdateでGround Query / Step Downへ再接地するため、
            // Platform変位を直接反映してから床面へSnapさせます。
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
            // Generation込みEntity Handleが無効、またはBodyがKinematicではなくなった場合は
            // 古いPlatform差分を新しいEntityへ適用しないよう即座に追跡を破棄します。
            ResetMovingPlatformTracking();
        }
    }

    const bool wasGrounded = m_Grounded;

    // まず既存Character Controller本体を実行します。
    // Ground / Wall / Step / Ceilingの責務は既存実装へ集約し、Moving Platform側で重複させません。
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
    // Platform上からJumpしたFrameだけ、Platformの水平速度をCharacter固有速度へ移します。
    // Platform搬送そのものは既にPositionへ適用済みなので、ここで継承するのは「離床後も残る慣性」です。
    // 鉛直成分はJumpSpeedを予測可能に保つため現段階では継承しません。
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

        // UpdateInternal()は既にこのFrameの入力移動を積分済みなので、Jump開始FrameにもPlatform慣性を
        // 見た目とStateの両方へ反映するため追加変位もCapsule Cast経路で適用します。
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
    // UpdateInternal()完了後の最終足元位置でもう一度Ground Entityを確認し、次Frame搬送の基準位置を保存します。
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

                // 新しく乗ったPlatformには前Frame差分が存在しないため速度0から開始します。
                if (samePlatform == false)
                {
                    m_MovingPlatformVelocity = math::Vec3{};
                }

                return true;
            }
        }
    }

    // Airborne / Static Ground / Planeへ移った場合はMoving Platform追跡を終了します。
    ResetMovingPlatformTracking();
    return true;
}

} // namespace Raven