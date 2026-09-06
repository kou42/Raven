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
inline bool NearlyEqual(float left, float right, float tolerance = 1.0e-4f)
{
    return std::fabs(left - right) <= tolerance;
}
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

// Dash StateをPhysics/Animationへ接続する前に、入力Edge・Timer・方向固定・開始Eventを独立検証します。
inline bool Run(std::string* errorMessage = nullptr)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    CharacterDashConfig config{};
    config.Speed = 10.0f;
    config.Duration = 0.20f;
    config.Cooldown = 0.50f;
    config.GroundedOnly = true;
    CharacterDashAction dash(config);

    if (dash.Update(true, math::Vec2{ 1.0f, 1.0f }, math::Vec3{ 0.0f, 0.0f, 1.0f }, true, 0.0f, errorMessage) == false)
    {
        return false;
    }
    if (Detail::Expect(dash.IsActive() && dash.StartedThisFrame(), "Dash開始State/Eventが不正です", errorMessage) == false)
    {
        return false;
    }

    const math::Vec3 firstVelocity = dash.GetHorizontalVelocity();
    const float expectedComponent = config.Speed / std::sqrt(2.0f);
    if (Detail::Expect(Detail::NearlyEqual(firstVelocity.x, expectedComponent) && Detail::NearlyEqual(firstVelocity.z, expectedComponent), "Dash速度または方向の正規化が不正です", errorMessage) == false)
    {
        return false;
    }

    if (dash.Update(true, math::Vec2{ -1.0f, 0.0f }, math::Vec3{ 0.0f, 0.0f, -1.0f }, true, 0.10f, errorMessage) == false)
    {
        return false;
    }
    const math::Vec3 heldVelocity = dash.GetHorizontalVelocity();
    if (Detail::Expect(dash.StartedThisFrame() == false && Detail::NearlyEqual(heldVelocity.x, firstVelocity.x) && Detail::NearlyEqual(heldVelocity.z, firstVelocity.z), "Dash Hold中に方向変更または開始Event再発火が発生しました", errorMessage) == false)
    {
        return false;
    }

    if (dash.Update(true, math::Vec2{ 0.0f, 1.0f }, math::Vec3{ 0.0f, 0.0f, 1.0f }, true, 0.11f, errorMessage) == false)
    {
        return false;
    }
    if (Detail::Expect(dash.IsActive() == false && dash.StartedThisFrame() == false, "Dash Hold中に再発火しました", errorMessage) == false)
    {
        return false;
    }

    if (dash.Update(false, math::Vec2{ 0.0f, 1.0f }, math::Vec3{ 0.0f, 0.0f, 1.0f }, true, 0.01f, errorMessage) == false
        || dash.Update(true, math::Vec2{ 0.0f, 1.0f }, math::Vec3{ 0.0f, 0.0f, 1.0f }, true, 0.01f, errorMessage) == false)
    {
        return false;
    }
    if (Detail::Expect(dash.IsActive() == false, "Cooldown中にDashが開始されました", errorMessage) == false)
    {
        return false;
    }

    if (dash.Update(false, math::Vec2{ 0.0f, 0.0f }, math::Vec3{ 0.0f, 0.0f, -1.0f }, true, 0.50f, errorMessage) == false
        || dash.Update(true, math::Vec2{ 0.0f, 0.0f }, math::Vec3{ 0.0f, 0.0f, -1.0f }, true, 0.0f, errorMessage) == false)
    {
        return false;
    }
    if (Detail::Expect(dash.IsActive() && dash.StartedThisFrame(), "Cooldown終了後にDashを再開できませんでした", errorMessage) == false)
    {
        return false;
    }

    const math::Vec3 fallbackVelocity = dash.GetHorizontalVelocity();
    if (Detail::Expect(Detail::NearlyEqual(fallbackVelocity.x, 0.0f) && Detail::NearlyEqual(fallbackVelocity.z, -config.Speed), "Move入力なしDashでCharacter Forward fallbackが使われていません", errorMessage) == false)
    {
        return false;
    }

    dash.Reset();
    if (Detail::Expect(dash.IsActive() == false && dash.IsCoolingDown() == false && dash.StartedThisFrame() == false, "Dash ResetでAction Stateが破棄されませんでした", errorMessage) == false)
    {
        return false;
    }

    if (dash.Update(false, math::Vec2{ 0.0f, 1.0f }, math::Vec3{ 0.0f, 0.0f, 1.0f }, false, 0.0f, errorMessage) == false
        || dash.Update(true, math::Vec2{ 0.0f, 1.0f }, math::Vec3{ 0.0f, 0.0f, 1.0f }, false, 0.0f, errorMessage) == false)
    {
        return false;
    }
    return Detail::Expect(dash.IsActive() == false && dash.StartedThisFrame() == false, "AirborneでGroundedOnly Dashが開始されました", errorMessage);
}

} // namespace CharacterDashActionSelfTests
} // namespace Raven
