#pragma once

#include "Raven/Math/MathVector.h"

#include <vector>

namespace Raven::ph
{
class VectorField;
class ScalarField;

enum class PhysicsFieldSlicePlane { XZ, XY, YZ };

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

struct PhysicsFieldDebugSlice
{
    static constexpr int PointsPerAxis = 17;
    static constexpr float Spacing = 0.5f;
    math::Vec3 Center{};
    PhysicsFieldSlicePlane Plane = PhysicsFieldSlicePlane::XZ;
    float Coordinate = 0.0f;
    float SampleSpacing = Spacing;
};

struct PhysicsFieldDebugScalarSample
{
    math::Vec3 Position;
    float Value;
};

std::vector<PhysicsFieldDebugScalarSample> SamplePhysicsScalarField(
    const ScalarField& field, const PhysicsFieldDebugSlice& slice);

// 行優先の格子を返します。等値線はこの順序で隣接するサンプルを参照できます。
std::vector<math::Vec3> BuildPhysicsFieldDebugSlicePositions(const PhysicsFieldDebugSlice& slice);
}
