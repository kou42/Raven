#pragma once

#include "Raven/Physics/Debug/PhysicsFieldDebugSampler.h"

#include <array>

namespace Raven::ph
{
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
}
