// Raven/Character/Tests/CharacterDashActionSelfTests.h
#pragma once

#include <cmath>
#include <string>

#include "Raven/Character/CharacterDashAction.h"

namespace Raven
{
namespace CharacterDashActionSelfTests
{
namespace Detail
{
inline bool NearlyEqual(float left, float right) { return std::fabs(left - right) <= 1.0e-4f; }
inline bool Expect(bool condition, const char* message, std::string* errorMessage)
{
    if (condition == true)
    {
        return true;
    }
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
    return false;
}
} // namespace Detail

// Physics/Animation接続前に、Dashの入力Edge・方向固定・Cooldown・開始Eventを独立検証します。
inline bool Run(std::string* errorMessage = nullptr)
{
    CharacterDashConfig config{};
    config.Speed = 10.0f;
    config.Duration = 0.20f;
    config.Cooldown = 0.50f;
    CharacterDashAction dash(config);

    if (dash.Update(true, math::Vec2{ 1.0f, 1.0f }, math::Vec3{ 0.0f, 0.0f, 1.0f }, true, 0.0f, errorMessage) == false)
    {
        return false;
    }
    if (Detail::Expect(dash.IsActive() == true && dash.StartedThisFrame() == true, "Dash開始Eventが不正です", errorMessage) == false)
    {
        return false;
    }

    const math::Vec3 initialVelocity = dash.GetHorizontalVelocity();
    const float component = config.Speed / std::sqrt(2.0f);
    if (Detail::Expect(Detail::NearlyEqual(initialVelocity.x, component) == true && Detail::NearlyEqual(initialVelocity.z, component) == true, "Dash方向の正規化が不正です", errorMessage) == false)
    {
        return false;
    }

    if (dash.Update(true, math::Vec2{ -1.0f, 0.0f }, math::Vec3{ 0.0f, 0.0f, -1.0f }, true, 0.10f, errorMessage) == false)
    {
        return false;
    }
    const math::Vec3 heldVelocity = dash.GetHorizontalVelocity();
    if (Detail::Expect(dash.StartedThisFrame() == false && Detail::NearlyEqual(heldVelocity.x, initialVelocity.x) == true && Detail::NearlyEqual(heldVelocity.z, initialVelocity.z) == true, "Dash Hold中に再発火または方向変更しました", errorMessage) == false)
    {
        return false;
    }

    if (dash.Update(true, math::Vec2{ 0.0f, 1.0f }, math::Vec3{ 0.0f, 0.0f, 1.0f }, true, 0.11f, errorMessage) == false)
    {
        return false;
    }
    if (Detail::Expect(dash.IsActive() == false, "Dash Duration終了後もActiveです", errorMessage) == false)
    {
        return false;
    }

    const bool releaseDuringCooldown = dash.Update(false, math::Vec2{ 0.0f, 1.0f }, math::Vec3{ 0.0f, 0.0f, 1.0f }, true, 0.01f, errorMessage);
    if (releaseDuringCooldown == false)
    {
        return false;
    }
    const bool pressDuringCooldown = dash.Update(true, math::Vec2{ 0.0f, 1.0f }, math::Vec3{ 0.0f, 0.0f, 1.0f }, true, 0.01f, errorMessage);
    if (pressDuringCooldown == false)
    {
        return false;
    }
    if (Detail::Expect(dash.IsActive() == false, "Cooldown中にDashが再開しました", errorMessage) == false)
    {
        return false;
    }

    // Cooldown終了後は新しいPress Edgeを受け付け、MoveなしならForwardへDashします。
    const bool releaseAfterCooldown = dash.Update(false, math::Vec2{ 0.0f, 0.0f }, math::Vec3{ 0.0f, 0.0f, -1.0f }, true, 0.50f, errorMessage);
    if (releaseAfterCooldown == false)
    {
        return false;
    }
    const bool pressAfterCooldown = dash.Update(true, math::Vec2{ 0.0f, 0.0f }, math::Vec3{ 0.0f, 0.0f, -1.0f }, true, 0.0f, errorMessage);
    if (pressAfterCooldown == false)
    {
        return false;
    }
    const math::Vec3 fallbackVelocity = dash.GetHorizontalVelocity();
    if (Detail::Expect(dash.StartedThisFrame() == true && Detail::NearlyEqual(fallbackVelocity.x, 0.0f) == true && Detail::NearlyEqual(fallbackVelocity.z, -config.Speed) == true, "Cooldown終了後のForward Dashが不正です", errorMessage) == false)
    {
        return false;
    }

    dash.Reset();
    if (Detail::Expect(dash.IsActive() == false && dash.IsCoolingDown() == false && dash.StartedThisFrame() == false, "Dash Resetが不完全です", errorMessage) == false)
    {
        return false;
    }

    const bool airborneRelease = dash.Update(false, math::Vec2{ 0.0f, 1.0f }, math::Vec3{ 0.0f, 0.0f, 1.0f }, false, 0.0f, errorMessage);
    if (airborneRelease == false)
    {
        return false;
    }
    const bool airbornePress = dash.Update(true, math::Vec2{ 0.0f, 1.0f }, math::Vec3{ 0.0f, 0.0f, 1.0f }, false, 0.0f, errorMessage);
    if (airbornePress == false)
    {
        return false;
    }
    return Detail::Expect(dash.IsActive() == false, "GroundedOnly Dashが空中で開始しました", errorMessage);
}

} // namespace CharacterDashActionSelfTests
} // namespace Raven
