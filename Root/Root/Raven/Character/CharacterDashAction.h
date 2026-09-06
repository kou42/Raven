// Raven/Character/CharacterDashAction.h
#pragma once

#include <algorithm>
#include <cmath>

#include "Raven/Math/MathVector.h"

namespace Raven
{

struct CharacterDashActionConfig
{
    float Speed = 12.0f;
    float Duration = 0.22f;
    float Cooldown = 0.35f;
};

// ============================================================================
// CharacterDashAction
// ============================================================================
// Dash入力のEdge・継続時間・Cooldown・開始方向だけを管理するGameplay Actionです。
// CollisionやTransform更新は行わず、現在Dash中に使うWorld XZ水平速度だけを生成します。
// 実際の移動はCharacterControllerが既存Capsule Cast / Step / Wall Slideへ流します。
class CharacterDashAction
{
public:
    CharacterDashAction() = default;
    explicit CharacterDashAction(const CharacterDashActionConfig& config)
        : m_Config(config)
    {
    }

    void SetConfig(const CharacterDashActionConfig& config) { m_Config = config; }
    const CharacterDashActionConfig& GetConfig() const { return m_Config; }

    // worldMoveはCamera-relative変換後のWorld XZ入力です。
    // Moveが無い場合はCharacter Forward(+Z)へDashします。
    void Update(
        bool dashPressed,
        const math::Vec2& worldMove,
        float characterYawRadians,
        bool canStart,
        float deltaTime)
    {
        m_StartedThisFrame = false;

        const float safeDeltaTime = std::isfinite(deltaTime)
            ? std::max(deltaTime, 0.0f)
            : 0.0f;

        if (m_CooldownRemaining > 0.0f)
        {
            m_CooldownRemaining = std::max(0.0f, m_CooldownRemaining - safeDeltaTime);
        }

        const bool dashPressedThisFrame = dashPressed && m_PreviousDashPressed == false;
        m_PreviousDashPressed = dashPressed;

        if (dashPressedThisFrame == true
            && m_Active == false
            && m_CooldownRemaining <= 0.0f
            && canStart == true
            && ValidateConfig() == true)
        {
            Start(worldMove, characterYawRadians);
        }

        if (m_Active == false)
        {
            return;
        }

        m_RemainingDuration = std::max(0.0f, m_RemainingDuration - safeDeltaTime);
        if (m_RemainingDuration <= 0.0f)
        {
            m_Active = false;
            m_CooldownRemaining = m_Config.Cooldown;
        }
    }

    void Reset()
    {
        m_Direction = math::Vec2{ 0.0f, 1.0f };
        m_RemainingDuration = 0.0f;
        m_CooldownRemaining = 0.0f;
        m_PreviousDashPressed = false;
        m_Active = false;
        m_StartedThisFrame = false;
    }

    bool IsActive() const { return m_Active; }
    bool WasStartedThisFrame() const { return m_StartedThisFrame; }
    float GetRemainingDuration() const { return m_RemainingDuration; }
    float GetCooldownRemaining() const { return m_CooldownRemaining; }
    const math::Vec2& GetDirection() const { return m_Direction; }

    math::Vec2 GetHorizontalVelocity() const
    {
        if (m_Active == false)
        {
            return math::Vec2{ 0.0f, 0.0f };
        }

        return math::Vec2{
            m_Direction.x * m_Config.Speed,
            m_Direction.y * m_Config.Speed
        };
    }

private:
    bool ValidateConfig() const
    {
        return std::isfinite(m_Config.Speed)
            && std::isfinite(m_Config.Duration)
            && std::isfinite(m_Config.Cooldown)
            && m_Config.Speed > 0.0f
            && m_Config.Duration > 0.0f
            && m_Config.Cooldown >= 0.0f;
    }

    void Start(const math::Vec2& worldMove, float characterYawRadians)
    {
        const float moveLengthSquared = worldMove.x * worldMove.x + worldMove.y * worldMove.y;
        if (moveLengthSquared > 1.0e-6f)
        {
            const float inverseLength = 1.0f / std::sqrt(moveLengthSquared);
            m_Direction = math::Vec2{
                worldMove.x * inverseLength,
                worldMove.y * inverseLength
            };
        }
        else
        {
            // CharacterControllerは+ZをForwardとしてYawをatan2(x, z)で管理します。
            m_Direction = math::Vec2{
                std::sin(characterYawRadians),
                std::cos(characterYawRadians)
            };
        }

        m_RemainingDuration = m_Config.Duration;
        m_Active = true;
        m_StartedThisFrame = true;
    }

private:
    CharacterDashActionConfig m_Config{};
    math::Vec2 m_Direction{ 0.0f, 1.0f };
    float m_RemainingDuration = 0.0f;
    float m_CooldownRemaining = 0.0f;
    bool m_PreviousDashPressed = false;
    bool m_Active = false;
    bool m_StartedThisFrame = false;
};

} // namespace Raven
