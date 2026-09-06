// Raven/Character/CharacterDashAction.h
#pragma once

#include <algorithm>
#include <cmath>

#include "Raven/Character/CharacterController.h"

namespace Raven
{

// ============================================================================
// CharacterDashActionConfig
// ============================================================================
struct CharacterDashActionConfig
{
    // Dash中にCharacterControllerへOverrideする水平速度[m/s]です。
    float Speed = 12.0f;

    // Dash速度を維持する時間[秒]です。
    float Duration = 0.22f;

    // Dash終了後、次のDash開始を許可するまでの待機時間[秒]です。
    float Cooldown = 0.35f;
};

// ============================================================================
// CharacterDashAction
// ============================================================================
// Dashの入力Edge・継続時間・Cooldown・開始方向を管理するGameplay Actionです。
// CharacterControllerのCollision処理そのものは所有せず、Dash中だけ
// CharacterControllerInput::HorizontalVelocityOverrideへWorld XZ速度を書き込みます。
//
// これによりDash移動も通常移動と同じCapsule Cast / Step Up / Wall Slide / Dynamic Push経路を通り、
// Transformへ直接加算するDash専用移動による壁抜けを防ぎます。
class CharacterDashAction
{
public:
    CharacterDashAction() = default;
    explicit CharacterDashAction(const CharacterDashActionConfig& config)
        : m_Config(config)
    {
    }

    void SetConfig(const CharacterDashActionConfig& config)
    {
        m_Config = config;
    }

    const CharacterDashActionConfig& GetConfig() const
    {
        return m_Config;
    }

    // input.MoveはCamera-relative変換後のWorld XZ方向を想定します。
    // Move入力が無い場合はcharacterYawRadiansからCharacter Forward(+Z)を作り、前方Dashします。
    // canStart=falseでは新規Dashを開始しませんが、すでに開始済みのDashは最後まで継続します。
    void Update(
        CharacterControllerInput& input,
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

        const bool dashPressed = input.Dash;
        const bool dashPressedThisFrame = dashPressed && m_PreviousDashPressed == false;
        m_PreviousDashPressed = dashPressed;

        if (dashPressedThisFrame == true
            && m_Active == false
            && m_CooldownRemaining <= 0.0f
            && canStart == true
            && ValidateConfig() == true)
        {
            Start(input, characterYawRadians);
        }

        if (m_Active == false)
        {
            return;
        }

        // Dash中は通常のWalk/Run/Sprint加減速を一時Overrideします。
        // CharacterController側はこの速度から従来の水平変位を作るため、Collision-aware経路を再利用できます。
        input.HasHorizontalVelocityOverride = true;
        input.HorizontalVelocityOverride = math::Vec2{
            m_Direction.x * m_Config.Speed,
            m_Direction.y * m_Config.Speed
        };

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

    bool IsActive() const
    {
        return m_Active;
    }

    bool WasStartedThisFrame() const
    {
        return m_StartedThisFrame;
    }

    float GetRemainingDuration() const
    {
        return m_RemainingDuration;
    }

    float GetCooldownRemaining() const
    {
        return m_CooldownRemaining;
    }

    const math::Vec2& GetDirection() const
    {
        return m_Direction;
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

    void Start(const CharacterControllerInput& input, float characterYawRadians)
    {
        const float moveLengthSquared = input.Move.x * input.Move.x + input.Move.y * input.Move.y;
        if (moveLengthSquared > 1.0e-6f)
        {
            const float inverseLength = 1.0f / std::sqrt(moveLengthSquared);
            m_Direction = math::Vec2{
                input.Move.x * inverseLength,
                input.Move.y * inverseLength
            };
        }
        else
        {
            // CharacterControllerは+ZをForwardとしてYawをatan2(x, z)で管理しています。
            // その規約と同じForwardを作ることで、無入力Dashでも現在の見た目方向と一致します。
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
