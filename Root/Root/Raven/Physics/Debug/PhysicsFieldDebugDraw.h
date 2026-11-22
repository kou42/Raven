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

bool BuildPhysicsFieldDebugArrow(const PhysicsFieldDebugSample& sample, float referenceMagnitude,
    float referenceLength, float maxLength, std::array<PhysicsFieldDebugLine, 3>& lines);
}
