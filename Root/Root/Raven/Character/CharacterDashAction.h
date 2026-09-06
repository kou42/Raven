// Raven/Character/CharacterDashAction.h
#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "Raven/Math/MathVector.h"

namespace Raven
{

// ============================================================================
// CharacterDashConfig
// ============================================================================
// DashはSprintの上位速度ではなく、短時間だけ発生するGameplay Actionとして扱います。
// そのためLocomotion BlendTreeの5番目のChildにはせず、Duration / Cooldownを持つ独立Stateにします。
struct CharacterDashConfig
{
    // Dash中に要求する水平速度[m/s]です。
    float Speed = 11.0f;
    // Dash速度を維持する時間[秒]です。
    float Duration = 0.18f;
    // Dash開始から次のDashを受け付けるまでの時間[秒]です。
    // Duration以上にすることで、Dash中の再入力による連続再始動を防ぎます。
    float Cooldown = 0.45f;
    // trueの場合は接地中だけDash開始を許可します。
    bool GroundedOnly = true;
};

// ============================================================================
// CharacterDashAction
// ============================================================================
// CharacterControllerの通常Locomotionとは独立した、短時間Dashの状態管理です。
// Transformは直接変更せず、Collision / Step / Wall SlideはCharacterControllerの既存Capsule経路へ任せます。
class CharacterDashAction
{
public:
    CharacterDashAction() = default;
    explicit CharacterDashAction(const CharacterDashConfig& config)
        : m_Config(config)
    {
    }

    void SetConfig(const CharacterDashConfig& config) { m_Config = config; }
    const CharacterDashConfig& GetConfig() const { return m_Config; }

    bool ValidateConfig(std::string* errorMessage = nullptr) const
    {
        if (errorMessage != nullptr)
        {
            errorMessage->clear();
        }
        if (std::isfinite(m_Config.Speed) == false
            || std::isfinite(m_Config.Duration) == false
            || std::isfinite(m_Config.Cooldown) == false
            || m_Config.Speed <= 0.0f
            || m_Config.Duration <= 0.0f
            || m_Config.Cooldown < m_Config.Duration)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Dash Configは Speed > 0, Duration > 0, Cooldown >= Duration を満たす必要があります";
            }
            return false;
        }
        return true;
    }

    // moveInputは X=Right, Y=Forward。入力が無い場合はfallbackForwardのXZ方向を使用します。
    bool Update(
        bool dashRequested,
        const math::Vec2& moveInput,
        const math::Vec3& fallbackForward,
        bool grounded,
        float deltaTime,
        std::string* errorMessage = nullptr)
    {
        if (errorMessage != nullptr)
        {
            errorMessage->clear();
        }
        if (ValidateConfig(errorMessage) == false)
        {
            return false;
        }
        if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Dash deltaTimeは0以上の有限値である必要があります";
            }
            return false;
        }

        // Animation One-Shot等へ渡す1Frame Eventです。外部でIsActiveの立ち上がりを再推測させません。
        m_StartedThisFrame = false;
        m_ActiveRemaining = std::max(0.0f, m_ActiveRemaining - deltaTime);
        m_CooldownRemaining = std::max(0.0f, m_CooldownRemaining - deltaTime);

        const bool pressedThisFrame = dashRequested == true && m_DashRequestedLastFrame == false;
        m_DashRequestedLastFrame = dashRequested;
        if (pressedThisFrame == false
            || m_ActiveRemaining > 0.0f
            || m_CooldownRemaining > 0.0f
            || (m_Config.GroundedOnly == true && grounded == false))
        {
            return true;
        }

        math::Vec3 dashDirection{ moveInput.x, 0.0f, moveInput.y };
        float directionLengthSquared = dashDirection.LengthSq();
        if (directionLengthSquared <= 1.0e-8f)
        {
            dashDirection = math::Vec3{ fallbackForward.x, 0.0f, fallbackForward.z };
            directionLengthSquared = dashDirection.LengthSq();
        }
        if (directionLengthSquared <= 1.0e-8f)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Dash方向を解決できません";
            }
            return false;
        }

        dashDirection /= std::sqrt(directionLengthSquared);
        m_Direction = dashDirection;
        m_ActiveRemaining = m_Config.Duration;
        m_CooldownRemaining = m_Config.Cooldown;
        m_StartedThisFrame = true;
        return true;
    }

    bool IsActive() const { return m_ActiveRemaining > 0.0f; }
    bool StartedThisFrame() const { return m_StartedThisFrame; }
    bool IsCoolingDown() const { return m_CooldownRemaining > 0.0f; }
    float GetActiveRemaining() const { return m_ActiveRemaining; }
    float GetCooldownRemaining() const { return m_CooldownRemaining; }
    const math::Vec3& GetDirection() const { return m_Direction; }

    math::Vec3 GetHorizontalVelocity() const
    {
        if (IsActive() == false)
        {
            return math::Vec3{ 0.0f, 0.0f, 0.0f };
        }
        return m_Direction * m_Config.Speed;
    }

    // Teleport / Scene切替 / Ragdoll遷移など、以前のAction Stateを持ち越してはいけない境界で使用します。
    void Reset()
    {
        m_Direction = math::Vec3{ 0.0f, 0.0f, 1.0f };
        m_ActiveRemaining = 0.0f;
        m_CooldownRemaining = 0.0f;
        m_DashRequestedLastFrame = false;
        m_StartedThisFrame = false;
    }

private:
    CharacterDashConfig m_Config{};
    math::Vec3 m_Direction{ 0.0f, 0.0f, 1.0f };
    float m_ActiveRemaining = 0.0f;
    float m_CooldownRemaining = 0.0f;
    bool m_DashRequestedLastFrame = false;
    bool m_StartedThisFrame = false;
};

} // namespace Raven
