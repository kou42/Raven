#include "Raven/Physics/Debug/PhysicsFieldDebugDraw.h"

#include <algorithm>
#include <cmath>

namespace Raven::ph
{
bool BuildPhysicsFieldDebugArrow(const PhysicsFieldDebugSample& sample, const PhysicsFieldDebugArrowStyle& style,
    float referenceLength, float maxLength, std::array<PhysicsFieldDebugLine, 3>& lines)
{
    const auto& value = sample.Value;
    if (std::isfinite(value.x) == false || std::isfinite(value.y) == false || std::isfinite(value.z) == false
        || std::isfinite(style.ReferenceMagnitude) == false || style.ReferenceMagnitude <= 0.0f
        || std::isfinite(referenceLength) == false || referenceLength <= 0.0f
        || std::isfinite(maxLength) == false || maxLength <= 0.0f)
    {
        return false;
    }

    // 有限な float の大きな値でも二乗で桁あふれしないよう、強度と正規化は double で計算します。
    const double magnitude = std::hypot(static_cast<double>(value.x),
        static_cast<double>(value.y), static_cast<double>(value.z));
    if (magnitude == 0.0)
    {
        return false;
    }
    const math::Vec3 direction{ static_cast<float>(value.x / magnitude),
        static_cast<float>(value.y / magnitude), static_cast<float>(value.z / magnitude) };
    const double relativeMagnitude = magnitude / style.ReferenceMagnitude;
    const float length = static_cast<float>(
        std::min(relativeMagnitude * referenceLength, static_cast<double>(maxLength)));
    const math::Vec3 tip = sample.Position + direction * length;
    if (tip == sample.Position)
    {
        return false;
    }

    // 向きとほぼ平行な軸を避け、矢尻の幅を確保します。
    const math::Vec3 up = std::abs(direction.y) < 0.9f ? math::Vec3{ 0, 1, 0 } : math::Vec3{ 1, 0, 0 };
    const math::Vec3 side = math::Vec3::Cross(direction, up).Normalized() * (length * 0.15f);
    const math::Vec3 headBase = tip - direction * (length * 0.3f);
    const float colorWeight = static_cast<float>(std::min(relativeMagnitude * 0.5, 1.0));
    const math::Vec3 color = style.WeakColor + (style.StrongColor - style.WeakColor) * colorWeight;
    lines = {{ { sample.Position, tip, color }, { tip, headBase + side, color }, { tip, headBase - side, color } }};
    return true;
}

bool TryGetPhysicsFieldDebugScalarColor(float value, float minimum, float maximum,
    const math::Vec3& lowColor, const math::Vec3& highColor, math::Vec3& color)
{
    if (std::isfinite(value) == false || std::isfinite(minimum) == false
        || std::isfinite(maximum) == false || minimum >= maximum)
    {
        return false;
    }
    const auto weight = static_cast<float>(std::clamp(
        (static_cast<double>(value) - minimum) / (static_cast<double>(maximum) - minimum), 0.0, 1.0));
    color = lowColor + (highColor - lowColor) * weight;
    return true;
}
}
