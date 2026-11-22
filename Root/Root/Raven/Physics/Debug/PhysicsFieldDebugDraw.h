#pragma once

#include "Raven/Physics/Debug/PhysicsFieldDebugSampler.h"

#include <array>
#include <span>

namespace Raven::ph
{
struct PhysicsSDFDebugSample;
struct PhysicsFieldDebugLine
{
    math::Vec3 Start;
    math::Vec3 End;
    math::Vec3 Color;
};

struct PhysicsFieldDebugArrowStyle
{
    float ReferenceMagnitude;
    math::Vec3 WeakColor;
    math::Vec3 StrongColor;
};

bool BuildPhysicsFieldDebugArrow(const PhysicsFieldDebugSample& sample, const PhysicsFieldDebugArrowStyle& style,
    float referenceLength, float maxLength, std::array<PhysicsFieldDebugLine, 3>& lines);

bool TryGetPhysicsFieldDebugScalarColor(float value, float minimum, float maximum,
    const math::Vec3& lowColor, const math::Vec3& highColor, math::Vec3& color);

std::vector<PhysicsFieldDebugLine> BuildPhysicsSDFZeroContour(std::span<const PhysicsSDFDebugSample> samples);
}
