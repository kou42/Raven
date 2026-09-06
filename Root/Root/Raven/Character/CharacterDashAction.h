// Raven/Character/CharacterDashAction.h
#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "Raven/Math/MathVector.h"

namespace Raven
{
struct CharacterDashConfig
{
    float Speed = 11.0f;
    float Duration = 0.18f;
    float Cooldown = 0.45f;
    bool GroundedOnly = true;
};

// DashはSprintの上位速度ではなくDuration/Cooldownを持つ独立Gameplay Actionです。
// Transformは直接変更せず、最終移動はCharacterControllerのCapsule Cast経路へ渡します。
class CharacterDashAction
{
public:
    CharacterDashAction() = default;
    explicit CharacterDashAction(const CharacterDashConfig& config) : m_Config(config) {}

    void SetConfig(const CharacterDashConfig& config) { m_Config = config; }
    const CharacterDashConfig& GetConfig() const { return m_Config; }

    bool ValidateConfig(std::string* errorMessage = nullptr) const
    {
        if (errorMessage != nullptr) { errorMessage->clear(); }
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

    bool Update(bool dashRequested, const math::Vec2& moveInput, const math::Vec3& fallbackForward, bool grounded, float deltaTime, std::string* errorMessage = nullptr)
    {
        if (errorMessage != nullptr) { errorMessage->clear(); }
        if (ValidateConfig(errorMessage) == false) { return false; }
        if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
        {
            if (errorMessage != nullptr) { *errorMessage = "Dash deltaTimeは0以上の有限値である必要があります"; }
            return false;
        }

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

        math::Vec3 direction{ moveInput.x, 0.0f, moveInput.y };
        float lengthSquared = direction.LengthSq();
        if (lengthSquared <= 1.0e-8f)
        {
            direction = { fallbackForward.x, 0.0f, fallbackForward.z };
            lengthSquared = direction.LengthSq();
        }
        if (lengthSquared <= 1.0e-8f)
        {
            if (errorMessage != nullptr) { *errorMessage = "Dash方向を解決できません"; }
            return false;
        }

        direction /= std::sqrt(lengthSquared);
        m_Direction = direction;
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
        if (IsActive() == false) { return { 0.0f, 0.0f, 0.0f }; }
        return m_Direction * m_Config.Speed;
    }

    void Reset()
    {
        m_Direction = { 0.0f, 0.0f, 1.0f };
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
