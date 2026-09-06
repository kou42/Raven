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
inline bool NearlyEqual(float a, float b) { return std::fabs(a - b) <= 1.0e-4f; }
inline bool Expect(bool value, const char* message, std::string* error)
{
    if (value == true)
    {
        return true;
    }
    if (error != nullptr)
    {
        *error = message;
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

    if (dash.Update(true, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, true, 0.0f, errorMessage) == false)
    {
        return false;
    }
    if (Detail::Expect(dash.IsActive() && dash.StartedThisFrame(), "Dash開始Eventが不正です", errorMessage) == false)
    {
        return false;
    }

    const math::Vec3 initialVelocity = dash.GetHorizontalVelocity();
    const float component = config.Speed / std::sqrt(2.0f);
    if (Detail::Expect(Detail::NearlyEqual(initialVelocity.x, component) && Detail::NearlyEqual(initialVelocity.z, component), "Dash方向の正規化が不正です", errorMessage) == false)
    {
        return false;
    }

    if (dash.Update(true, { -1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, true, 0.10f, errorMessage) == false)
    {
        return false;
    }
    const math::Vec3 heldVelocity = dash.GetHorizontalVelocity();
    if (Detail::Expect(dash.StartedThisFrame() == false && Detail::NearlyEqual(heldVelocity.x, initialVelocity.x) && Detail::NearlyEqual(heldVelocity.z, initialVelocity.z), "Dash Hold中に再発火または方向変更しました", errorMessage) == false)
    {
        return false;
    }

    if (dash.Update(true, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, true, 0.11f, errorMessage) == false)
    {
        return false;
    }
    if (Detail::Expect(dash.IsActive() == false, "Dash Duration終了後もActiveです", errorMessage) == false)
    {
        return false;
    }

    if (dash.Update(false, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, true, 0.01f, errorMessage) == false
        || dash.Update(true, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, true, 0.01f, errorMessage) == false)
    {
        return false;
    }
    if (Detail::Expect(dash.IsActive() == false, "Cooldown中にDashが再開しました", errorMessage) == false)
    {
        return false;
    }

    // Cooldown終了後は新しいPress Edgeを受け付け、MoveなしならForwardへDashします。
    if (dash.Update(false, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, true, 0.50f, errorMessage) == false
        || dash.Update(true, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, true, 0.0f, errorMessage) == false)
    {
        return false;
    }
    const math::Vec3 fallbackVelocity = dash.GetHorizontalVelocity();
    if (Detail::Expect(dash.StartedThisFrame() && Detail::NearlyEqual(fallbackVelocity.x, 0.0f) && Detail::NearlyEqual(fallbackVelocity.z, -config.Speed), "Cooldown終了後のForward Dashが不正です", errorMessage) == false)
    {
        return false;
    }

    dash.Reset();
    if (Detail::Expect(dash.IsActive() == false && dash.IsCoolingDown() == false && dash.StartedThisFrame() == false, "Dash Resetが不完全です", errorMessage) == false)
    {
        return false;
    }

    if (dash.Update(false, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, false, 0.0f, errorMessage) == false
        || dash.Update(true, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, false, 0.0f, errorMessage) == false)
    {
        return false;
    }
    return Detail::Expect(dash.IsActive() == false, "GroundedOnly Dashが空中で開始しました", errorMessage);
}

} // namespace CharacterDashActionSelfTests
} // namespace Raven
