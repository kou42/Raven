// Raven/Physics/PhysicsWorldGroundQuery.cpp
#include "Raven/Physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>

#include "Raven/Scene/Scene.h"

namespace Raven
{
namespace ph
{

bool PhysicsWorld::GroundQuery(
    Scene& scene,
    const math::Vec3& origin,
    const PhysicsGroundQuerySettings& settings,
    PhysicsGroundQueryHit& outHit)
{
    // ========================================================================
    // Input Validation
    // ========================================================================
    // Ground QueryはCharacter ControllerだけでなくRagdoll復帰からも利用する公開APIなので、
    // NaN / 負距離などをここで拒否してPhysics内部へ不正値を流さないようにします。
    if (std::isfinite(origin.x) == false
        || std::isfinite(origin.y) == false
        || std::isfinite(origin.z) == false
        || std::isfinite(settings.MaxDistance) == false
        || std::isfinite(settings.MaxSlopeRadians) == false)
    {
        return false;
    }

    if (settings.MaxDistance < 0.0f
        || settings.MaxSlopeRadians < 0.0f)
    {
        return false;
    }

    constexpr float HalfPi = 1.57079632679489661923f;
    const float maxSlopeRadians = std::min(settings.MaxSlopeRadians, HalfPi);

    PhysicsRayCastFilter filter{};
    filter.IncludeStatic = settings.IncludeStatic;
    filter.IncludeKinematic = settings.IncludeKinematic;
    filter.IncludeDynamic = settings.IncludeDynamic;
    filter.IncludePlanes = settings.IncludePlanes;

    PhysicsRayCastHit rayHit{};
    if (RayCast(
            scene,
            origin,
            math::Vec3{ 0.0f, -1.0f, 0.0f },
            settings.MaxDistance,
            filter,
            rayHit) == false)
    {
        return false;
    }

    // ========================================================================
    // Walkable Slope Filter
    // ========================================================================
    // Ground NormalとWorld Upのdot = cos(theta)なので、Normal.yだけで斜面角度を判定できます。
    // 上向き面のみを床として扱うことで、崖の裏面や天井下面をCharacterのGroundとして誤採用しません。
    const math::Vec3 normal = rayHit.Normal.Normalized();
    if (normal.LengthSq() <= 1.0e-12f)
    {
        return false;
    }

    const float minimumUpDot = std::cos(maxSlopeRadians);
    if (normal.y < minimumUpDot)
    {
        return false;
    }

    outHit.HitEntity = rayHit.HitEntity;
    outHit.Point = rayHit.Point;
    outHit.Normal = normal;
    outHit.Distance = rayHit.Fraction;
    return true;
}

} // namespace ph
} // namespace Raven
