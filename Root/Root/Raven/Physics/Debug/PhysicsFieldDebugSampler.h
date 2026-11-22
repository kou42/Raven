#pragma once

#include "Raven/Math/MathVector.h"

#include <vector>

namespace Raven::ph
{
class VectorField;

struct PhysicsFieldDebugGrid
{
    static constexpr int MaxPointsPerAxis = 11;
    static constexpr float MinSpacing = 0.25f;
    static constexpr float MaxSpacing = 5.0f;

    math::Vec3 Center{};
    int PointsPerAxis = 7;
    float Spacing = 1.0f;

    float GetSpacing() const;
    float GetHalfExtent() const;
};

struct PhysicsFieldDebugSample
{
    math::Vec3 Position;
    math::Vec3 Value;
};

std::vector<PhysicsFieldDebugSample> SamplePhysicsVectorField(
    const VectorField& field, const PhysicsFieldDebugGrid& grid);
}
