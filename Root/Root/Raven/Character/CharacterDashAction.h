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
    // 将来Air Dashを追加する場合も既存Dashの規則を壊さず拡張できます。
    bool GroundedOnly = true;
};

// ============================================================================
// CharacterDashAction
// ============================================================================
// CharacterControllerの通常Locomotionとは独立した、短時間Dashの状態管理です。
//
// 重要:
// - Dash入力は押下Edgeで開始し、Button Holdでは再発火しない
// - 開始時の方向をDash終了まで固定する
// - Move入力が無い場合はCharacter ForwardへDashする
// - Collision / Step / Wall SlideはCharacterController側の既存Capsule経路へ任せる
//
// このクラスは「Dashを開始できるか」「現在どの水平速度を要求するか」だけを担当します。
// 実際のTransform移動を直接行わないことで、Dashだけ壁抜けする別移動経路を作らない設計です。
class CharacterDashAction
{
public:
    CharacterDashAction() = default;
    explicit CharacterDashAction(const CharacterDashConfig& config)
        : m_Config(config)
    {
    }

    void SetConfig(const CharacterDashConfig& config)
    {
        m_Config = config;
    }

    const CharacterDashConfig& GetConfig() const
    {
        return m_Config;
    }

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

    // 1Frame分のDash Stateを更新します。
    // moveInputはCharacterControllerInput::Moveと同じく X=Right, Y=Forward のWorld XZ入力を想定します。
    // fallbackForwardは入力が無い場合に使うCharacter Forwardで、XZ成分だけを利用します。
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

        // StartedThisFrameはAnimation One-Shot等へ渡す1Frame Eventです。
        // IsActive()の立ち上がりを外部で再推測させず、Gameplay State自身を正規のEvent発生元にします。
        m_StartedThisFrame = false;

        // Timerは入力判定より先に進めます。Cooldown=0へ到達したFrameから新しい押下Edgeを受け付けます。
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

        // 移動入力が無い場合は現在のCharacter ForwardへDashします。
        // Idle Dashでも方向が未定義にならず、Camera-relative入力へも依存しません。
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

    bool IsActive() const
    {
        return m_ActiveRemaining > 0.0f;
    }

    // Dash開始が成立したFrameだけtrueです。
    // Roll等のOne-Shot AnimationはこのEventで開始し、Button HoldやCooldown中には再発火させません。
    bool StartedThisFrame() const
    {
        return m_StartedThisFrame;
    }

    bool IsCoolingDown() const
    {
        return m_CooldownRemaining > 0.0f;
    }

    float GetActiveRemaining() const
    {
        return m_ActiveRemaining;
    }

    float GetCooldownRemaining() const
    {
        return m_CooldownRemaining;
    }

    const math::Vec3& GetDirection() const
    {
        return m_Direction;
    }

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
