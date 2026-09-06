// Raven/Character/CharacterDashAction.h
#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "Raven/Math/MathVector.h"

namespace Raven
{

// DashはSprintの上位速度ではなく、Duration / Cooldownを持つ独立Gameplay Actionです。
struct CharacterDashConfig
{
    float Speed = 11.0f;
    float Duration = 0.18f;
    float Cooldown = 0.45f;
    bool GroundedOnly = true;
};

// ============================================================================
// CharacterDashAction
// ============================================================================
// このStateはDashの開始可否・方向・Timerだけを担当し、Transformを直接変更しません。
// 最終的な水平速度はCharacterControllerの既存Capsule Cast / Step / Wall Slide経路へ渡す設計です。
// Dash専用の移動経路を作らないことで、通常移動と異なる壁抜けや段差判定を発生させません。
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

    // Button Level値を内部でPress Edgeへ変換します。Holdでは再発火しません。
    // moveInputはWorld XZへ変換済みの X=Right / Y=Forward を想定します。
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

        // Animation One-Shot等がDash開始を正確に検出できるよう、開始EventをState自身が発行します。
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

        // 開始時に方向を確定し、Dash中の入力変更では方向転換しません。
        math::Vec3 direction{ moveInput.x, 0.0f, moveInput.y };
        float lengthSquared = direction.LengthSq();
        if (lengthSquared <= 1.0e-8f)
        {
            // Idle Dashでは現在のCharacter Forwardを使用します。
            direction = math::Vec3{ fallbackForward.x, 0.0f, fallbackForward.z };
            lengthSquared = direction.LengthSq();
        }
        if (lengthSquared <= 1.0e-8f)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Dash方向を解決できません";
            }
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
    // Roll等のOne-Shot開始用。Dashが実際に成立したFrameだけtrueです。
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

    // Scene切替 / Teleport / Ragdoll遷移では以前のCooldownやButton履歴を持ち越しません。
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
