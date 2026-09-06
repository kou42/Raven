// Raven/Character/CharacterDashActionSelfTests.cpp
#include "Raven/Character/CharacterDashAction.h"

#include <cmath>
#include <string>

namespace Raven
{
namespace CharacterDashActionSelfTests
{
namespace
{

bool NearlyEqual(float left, float right, float tolerance = 1.0e-4f)
{
    return std::fabs(left - right) <= tolerance;
}

bool Expect(bool condition, const char* message, std::string* errorMessage)
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

} // namespace

// Dashの最小State MachineをEngine Runtimeへ接続する前に独立検証します。
// 特にButton Holdの再発火防止とCooldownは入力Frame順序に依存するため、
// TransformやPhysicsを使わないこのTestで仕様を固定しておきます。
bool Run(std::string* errorMessage)
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

    // Move入力がある場合はその方向を正規化してDash方向として固定します。
    if (dash.Update(
            true,
            math::Vec2{ 1.0f, 1.0f },
            math::Vec3{ 0.0f, 0.0f, 1.0f },
            true,
            0.0f,
            errorMessage) == false)
    {
        return false;
    }

    if (Expect(dash.IsActive(), "Dashが押下Edgeで開始されませんでした", errorMessage) == false)
    {
        return false;
    }

    const math::Vec3 firstVelocity = dash.GetHorizontalVelocity();
    const float expectedComponent = config.Speed / std::sqrt(2.0f);
    if (Expect(
            NearlyEqual(firstVelocity.x, expectedComponent)
                && NearlyEqual(firstVelocity.z, expectedComponent),
            "Dash速度または方向の正規化が不正です",
            errorMessage) == false)
    {
        return false;
    }

    // Dash中にMove方向を変えても、開始時に確定した方向は終了まで変更しません。
    if (dash.Update(
            true,
            math::Vec2{ -1.0f, 0.0f },
            math::Vec3{ 0.0f, 0.0f, -1.0f },
            true,
            0.10f,
            errorMessage) == false)
    {
        return false;
    }

    const math::Vec3 heldVelocity = dash.GetHorizontalVelocity();
    if (Expect(
            NearlyEqual(heldVelocity.x, firstVelocity.x)
                && NearlyEqual(heldVelocity.z, firstVelocity.z),
            "Dash中に方向が変更されました",
            errorMessage) == false)
    {
        return false;
    }

    // Buttonを押したままDurationを超えても再発火しません。
    if (dash.Update(
            true,
            math::Vec2{ 0.0f, 1.0f },
            math::Vec3{ 0.0f, 0.0f, 1.0f },
            true,
            0.11f,
            errorMessage) == false)
    {
        return false;
    }

    if (Expect(dash.IsActive() == false, "Dash Hold中に再発火しました", errorMessage) == false)
    {
        return false;
    }

    // 一度Buttonを離してもCooldown中は再発火しません。
    if (dash.Update(
            false,
            math::Vec2{ 0.0f, 1.0f },
            math::Vec3{ 0.0f, 0.0f, 1.0f },
            true,
            0.01f,
            errorMessage) == false
        || dash.Update(
            true,
            math::Vec2{ 0.0f, 1.0f },
            math::Vec3{ 0.0f, 0.0f, 1.0f },
            true,
            0.01f,
            errorMessage) == false)
    {
        return false;
    }

    if (Expect(dash.IsActive() == false, "Cooldown中にDashが開始されました", errorMessage) == false)
    {
        return false;
    }

    // Cooldown終了後は、Release -> Pressの新しいEdgeで再び開始できます。
    if (dash.Update(
            false,
            math::Vec2{ 0.0f, 0.0f },
            math::Vec3{ 0.0f, 0.0f, -1.0f },
            true,
            0.50f,
            errorMessage) == false
        || dash.Update(
            true,
            math::Vec2{ 0.0f, 0.0f },
            math::Vec3{ 0.0f, 0.0f, -1.0f },
            true,
            0.0f,
            errorMessage) == false)
    {
        return false;
    }

    if (Expect(dash.IsActive(), "Cooldown終了後にDashを再開できませんでした", errorMessage) == false)
    {
        return false;
    }

    const math::Vec3 fallbackVelocity = dash.GetHorizontalVelocity();
    if (Expect(
            NearlyEqual(fallbackVelocity.x, 0.0f)
                && NearlyEqual(fallbackVelocity.z, -config.Speed),
            "Move入力なしDashでCharacter Forward fallbackが使われていません",
            errorMessage) == false)
    {
        return false;
    }

    dash.Reset();
    if (Expect(
            dash.IsActive() == false && dash.IsCoolingDown() == false,
            "Dash ResetでAction Stateが破棄されませんでした",
            errorMessage) == false)
    {
        return false;
    }

    // GroundedOnly=trueではAirborneから開始できません。
    if (dash.Update(
            false,
            math::Vec2{ 0.0f, 1.0f },
            math::Vec3{ 0.0f, 0.0f, 1.0f },
            false,
            0.0f,
            errorMessage) == false
        || dash.Update(
            true,
            math::Vec2{ 0.0f, 1.0f },
            math::Vec3{ 0.0f, 0.0f, 1.0f },
            false,
            0.0f,
            errorMessage) == false)
    {
        return false;
    }

    return Expect(dash.IsActive() == false, "AirborneでGroundedOnly Dashが開始されました", errorMessage);
}

} // namespace CharacterDashActionSelfTests
} // namespace Raven
