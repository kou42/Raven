// Raven/Character/CharacterControllerMovingPlatform.cpp
#include "Raven/Character/CharacterController.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

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

bool IsTrackedKinematicGround(Scene& scene, Entity entity, TransformComponent*& outTransform)
{
    outTransform = nullptr;

    if (scene.IsEntityAlive(entity) == false)
    {
        return false;
    }

    TransformComponent* transform = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
    const RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (transform == nullptr || rigidBody == nullptr || rigidBody->Type != BodyType::Kinematic)
    {
        return false;
    }

    outTransform = transform;
    return true;
}

bool ContainsEntity(const std::vector<Entity>& entities, Entity target)
{
    for (const Entity& entity : entities)
    {
        if (entity == target)
        {
            return true;
        }
    }
    return false;
}

} // namespace

void CharacterController::ResetCrushTracking()
{
    // Scene切替 / Teleport / Ragdoll切替などでは、前Sceneや前状態のCrush履歴を
    // 次のCharacter状態へ持ち越してはいけません。瞬間状態と連続Exposureをまとめて破棄します。
    m_IsCrushed = false;
    m_CrushStrength = 0.0f;
    m_CrushDuration = 0.0f;
    m_CrushExposure = 0.0f;
}

void CharacterController::ResetMovingPlatformTracking()
{
    // この関数はStatic Groundへ降りた通常Frameでも内部から呼ばれるため、Crush履歴は触りません。
    // Scene切替 / Teleport / Ragdoll切替のように両方の履歴を破棄したい境界では、呼び出し側が
    // ResetMovingPlatformTracking() と ResetCrushTracking() をそれぞれ明示的に呼びます。
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
        return SetMovingPlatformError(errorMessage, "Moving Platform UpdateのdeltaTimeは0以上の有限値である必要があります");
    }
    if (std::isfinite(jumpPlatformHorizontalVelocityScale) == false || jumpPlatformHorizontalVelocityScale < 0.0f)
    {
        return SetMovingPlatformError(errorMessage, "Moving PlatformのJump速度継承率は0以上の有限値である必要があります");
    }

    // Crush状態は「現在Frameで実際に押し潰されているか」を表します。
    // m_CrushDuration / m_CrushExposure はこの時点ではResetせず、後段の判定結果がCrushなら加算、
    // Crushでなければ0へ戻します。これにより連続Crushだけを時間方向へ蓄積できます。
    m_IsCrushed = false;
    m_CrushStrength = 0.0f;

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
        if (IsTrackedKinematicGround(scene, m_MovingPlatformEntity, platformTransform) == true)
        {
            wasOnMovingPlatform = true;
            const math::Vec3 platformDisplacement = platformTransform->Position - m_MovingPlatformPosition;

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
            const math::Vec3 horizontalPlatformDisplacement{ platformDisplacement.x, 0.0f, platformDisplacement.z };
            if (horizontalPlatformDisplacement.LengthSq() > 1.0e-12f)
            {
                if (ResolvePhysicsMovement(scene, horizontalPlatformDisplacement, transform, errorMessage) == false)
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
                if (scene.GetPhysicsWorld().CapsuleCast(scene, transform.Position, upwardDisplacement, castSettings, hit) == true)
                {
                    const float safeFraction = std::clamp(hit.Fraction - 1.0e-4f, 0.0f, 1.0f);
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

    // ========================================================================
    // Incoming Dynamic Body -> Character
    // ========================================================================
    // Character自身はKinematic Bodyなので、Physics SolverだけではDynamic Bodyから押し返されません。
    // Dynamic Bodyの1Frame分の水平移動を相対運動へ変換し、Character Capsuleを逆方向へSweepします。
    //
    // 複数Bodyが同時にCharacterへ到達する場合は、各BodyについてHit後に残る変位を集め、平均して
    // 1つのCharacter変位へ統合します。単純加算にしない理由は、同速度の箱が2個重なっただけで
    // Character速度が2倍になる非物理的な増幅を避けるためです。反対側から同時に押された場合は
    // ベクトルが相殺されるため、Characterはその場に留まり「挟まれている」状態になります。
    if (m_Config.EnableDynamicBodyInteraction == true && deltaTime > math::Epsilon)
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

        math::Vec3 accumulatedDisplacement{};
        math::Vec3 accumulatedVelocity{};
        uint32_t incomingBodyCount = 0u;
        float strongestIncomingSpeed = 0.0f;
        std::vector<Entity> processedHitEntities;

        for (auto [sourceEntity, sourceRigidBody] : scene.View<RigidBodyComponent>())
        {
            if (sourceRigidBody.Type != BodyType::Dynamic || sourceRigidBody.InverseMass <= 0.0f)
            {
                continue;
            }

            const math::Vec3 sourceHorizontalVelocity{ sourceRigidBody.LinearVelocity.x, 0.0f, sourceRigidBody.LinearVelocity.z };
            const math::Vec3 sourceDisplacement = sourceHorizontalVelocity * deltaTime;
            if (sourceDisplacement.LengthSq() <= 1.0e-12f)
            {
                continue;
            }

            // Moving obstacleを固定した相対座標系では、CharacterがBody移動と逆方向へ動くと考えます。
            ph::PhysicsCapsuleCastHit dynamicHit{};
            if (scene.GetPhysicsWorld().CapsuleCast(scene, transform.Position, -sourceDisplacement, dynamicCastSettings, dynamicHit) == false)
            {
                continue;
            }

            const RigidBodyComponent* hitRigidBody = scene.TryGetComponent<RigidBodyComponent>(dynamicHit.HitEntity.GetIndex());
            if (hitRigidBody == nullptr || hitRigidBody->Type != BodyType::Dynamic || hitRigidBody->InverseMass <= 0.0f)
            {
                continue;
            }

            const math::Vec3 hitHorizontalVelocity{ hitRigidBody->LinearVelocity.x, 0.0f, hitRigidBody->LinearVelocity.z };
            const math::Vec3 hitDisplacement = hitHorizontalVelocity * deltaTime;
            if (hitDisplacement.LengthSq() <= 1.0e-12f)
            {
                continue;
            }

            // ----------------------------------------------------------------
            // Hit Entity verification
            // ----------------------------------------------------------------
            // CapsuleCastはDynamic全体から最初のHitを返すため、sourceEntityの速度で行ったSweepが
            // 別Bodyへ当たる場合があります。そのまま採用すると「Aの速度でBの衝突時刻を計算する」
            // ことになるため、HitしたBody自身の速度でもう一度Sweepし、同じEntityへ当たることを確認します。
            if (dynamicHit.HitEntity != sourceEntity)
            {
                ph::PhysicsCapsuleCastHit verifiedHit{};
                if (scene.GetPhysicsWorld().CapsuleCast(scene, transform.Position, -hitDisplacement, dynamicCastSettings, verifiedHit) == false)
                {
                    continue;
                }
                if (verifiedHit.HitEntity != dynamicHit.HitEntity)
                {
                    continue;
                }
                dynamicHit = verifiedHit;
            }

            // 同じHit Entityが複数sourceのSweepから見つかっても1回だけ集計します。
            if (ContainsEntity(processedHitEntities, dynamicHit.HitEntity) == true)
            {
                continue;
            }
            processedHitEntities.push_back(dynamicHit.HitEntity);

            // Fraction=0なら既に接触しているため1Frame分、Fraction=1ならFrame末尾接触なので0です。
            const float remainingTimeFraction = std::clamp(1.0f - dynamicHit.Fraction, 0.0f, 1.0f);
            if (remainingTimeFraction <= math::Epsilon)
            {
                continue;
            }

            const float hitSpeed = std::sqrt(hitHorizontalVelocity.LengthSq());
            strongestIncomingSpeed = std::max(strongestIncomingSpeed, hitSpeed);
            accumulatedDisplacement += hitHorizontalVelocity * deltaTime * remainingTimeFraction;
            accumulatedVelocity += hitHorizontalVelocity * remainingTimeFraction;
            ++incomingBodyCount;
        }

        if (incomingBodyCount > 0u)
        {
            const float inverseBodyCount = 1.0f / static_cast<float>(incomingBodyCount);
            const math::Vec3 incomingDisplacement = accumulatedDisplacement * inverseBodyCount;
            const math::Vec3 incomingVelocity = accumulatedVelocity * inverseBodyCount;

            // ====================================================================
            // Opposing Dynamic Bodies Crush
            // ====================================================================
            // 左右など反対方向から複数Bodyに押されると、平均変位だけ見れば0付近になります。
            // しかしstrongestIncomingSpeedは残るため、「強い入力が存在するのに合成速度だけが小さい」
            // 状態を挟み込みとして検出できます。
            const float combinedIncomingSpeed = std::sqrt(incomingVelocity.LengthSq());
            if (incomingBodyCount >= 2u
                && strongestIncomingSpeed >= m_Config.CrushMinIncomingSpeed
                && combinedIncomingSpeed <= strongestIncomingSpeed * m_Config.CrushOpposingVelocityRatio)
            {
                m_IsCrushed = true;
                m_CrushStrength = strongestIncomingSpeed;
            }

            // PositionだけでなくController速度へも反映します。押された後に入力が無ければ、通常の
            // Decelerationに従って減衰するため、外力由来の短い慣性として扱えます。
            m_Velocity.x = incomingVelocity.x;
            m_Velocity.z = incomingVelocity.z;

            if (incomingDisplacement.LengthSq() > 1.0e-12f)
            {
                // 押された先にStatic Wall / Kinematic / Dynamic Bodyが存在しても、通常のCharacter
                // MoveAndSlide経路で停止します。同時に「要求変位に対してどれだけ移動できたか」を
                // 比較することで、Dynamic Body -> Character -> Wall の押し潰しも検出します。
                const math::Vec3 positionBeforePush = transform.Position;
                if (ResolvePhysicsMovement(scene, incomingDisplacement, transform, errorMessage) == false)
                {
                    return false;
                }

                const math::Vec3 actualDisplacement = transform.Position - positionBeforePush;
                const float requestedDistanceSquared = incomingDisplacement.LengthSq();
                if (requestedDistanceSquared > 1.0e-12f
                    && strongestIncomingSpeed >= m_Config.CrushMinIncomingSpeed)
                {
                    const float requestedDistance = std::sqrt(requestedDistanceSquared);
                    const float actualDistance = std::sqrt(actualDisplacement.LengthSq());
                    const float movementRatio = actualDistance / requestedDistance;
                    if (movementRatio <= m_Config.CrushBlockedMovementRatio)
                    {
                        m_IsCrushed = true;
                        m_CrushStrength = std::max(m_CrushStrength, strongestIncomingSpeed);
                    }
                }
            }
        }
    }

    // ========================================================================
    // Continuous Crush Exposure
    // ========================================================================
    // IsCrushed()は現在Frameの瞬間状態のまま維持し、その上に連続時間とExposureを積みます。
    // Exposure = Σ(CrushStrength * dt) とし、同じ継続時間でも強い押し潰しほど大きな値になります。
    // 圧力が1Frameでも消えた場合は連続状態が途切れたとみなし、両方を0へ戻します。
    if (m_IsCrushed == true)
    {
        m_CrushDuration += deltaTime;
        m_CrushExposure += m_CrushStrength * deltaTime;
    }
    else
    {
        m_CrushDuration = 0.0f;
        m_CrushExposure = 0.0f;
    }

    const bool wasGrounded = m_Grounded;

    // まず既存Character Controller本体を実行します。
    // Ground / Wall / Step / Ceilingの責務は既存実装へ集約し、Moving Platform側で重複させません。
    if (UpdateInternal(input, deltaTime, &scene, transform, errorMessage) == false)
    {
        return false;
    }

    // ========================================================================
    // Jump Velocity Inheritance
    // ========================================================================
    // Platform上からJumpしたFrameだけ、Platformの水平速度をCharacter固有速度へ移します。
    // Platform搬送そのものは既にPositionへ適用済みなので、ここで継承するのは「離床後も残る慣性」です。
    // 鉛直成分はJumpSpeedを予測可能に保つため現段階では継承しません。
    if (input.Jump == true && wasGrounded == true && wasOnMovingPlatform == true && m_Grounded == false && jumpPlatformHorizontalVelocityScale > 0.0f)
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
            if (ResolvePhysicsMovement(scene, inheritedDisplacement, transform, errorMessage) == false)
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

        const math::Vec3 probeOrigin = transform.Position + math::Vec3{ 0.0f, m_Config.GroundProbeStartOffset, 0.0f };
        ph::PhysicsGroundQueryHit groundHit{};
        if (scene.GetPhysicsWorld().GroundQuery(scene, probeOrigin, groundSettings, groundHit) == true)
        {
            TransformComponent* platformTransform = nullptr;
            if (IsTrackedKinematicGround(scene, groundHit.HitEntity, platformTransform) == true)
            {
                const bool samePlatform = m_HasMovingPlatform == true && m_MovingPlatformEntity == groundHit.HitEntity;
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