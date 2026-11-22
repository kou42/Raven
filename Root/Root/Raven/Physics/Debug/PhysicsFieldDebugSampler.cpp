#include "Raven/Physics/Debug/PhysicsFieldDebugSampler.h"

#include "Raven/Physics/Field/PhysicsField.h"

#include <algorithm>
#include <cmath>

namespace Raven::ph
{
float PhysicsFieldDebugGrid::GetSpacing() const
{
    return std::isfinite(Spacing) ? std::clamp(Spacing, MinSpacing, MaxSpacing) : 1.0f;
}

float PhysicsFieldDebugGrid::GetHalfExtent() const
{
    const int count = std::clamp(PointsPerAxis, 1, MaxPointsPerAxis);
    return GetSpacing() * static_cast<float>(count - 1) * 0.5f;
}

std::vector<PhysicsFieldDebugSample> SamplePhysicsVectorField(
    const VectorField& field, const PhysicsFieldDebugGrid& grid)
{
    const int count = std::clamp(grid.PointsPerAxis, 1, PhysicsFieldDebugGrid::MaxPointsPerAxis);
    const float halfExtent = grid.GetHalfExtent();
    const float spacing = grid.GetSpacing();
    std::vector<PhysicsFieldDebugSample> samples;
    samples.reserve(count * count * count);
    for (int z = 0; z < count; ++z)
    {
        for (int y = 0; y < count; ++y)
        {
            for (int x = 0; x < count; ++x)
            {
                // 偶数個でも中心を基準に対称に配置し、表示枠と同じワールド座標を使います。
                const math::Vec3 position = grid.Center + math::Vec3{
                    x * spacing - halfExtent, y * spacing - halfExtent, z * spacing - halfExtent };
                if (std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z))
                {
                    samples.push_back({ position, field.Evaluate(position) });
                }
            }
        }
    }
    return samples;
}

std::vector<math::Vec3> BuildPhysicsFieldDebugSlicePositions(const PhysicsFieldDebugSlice& slice)
{
    // 直交2軸は場の中心を基準にし、法線方向だけを指定のワールド座標に固定します。
    const int horizontal = slice.Plane == PhysicsFieldSlicePlane::YZ ? 1 : 0;
    const int vertical = slice.Plane == PhysicsFieldSlicePlane::XY ? 1 : 2;
    const int normal = 3 - horizontal - vertical;
    math::Vec3 position = slice.Center;
    position[normal] = slice.Coordinate;
    if (std::isfinite(position.x) == false || std::isfinite(position.y) == false || std::isfinite(position.z) == false)
    {
        return {};
    }

    constexpr int count = PhysicsFieldDebugSlice::PointsPerAxis;
    const float spacing = std::isfinite(slice.SampleSpacing)
        ? std::clamp(slice.SampleSpacing, PhysicsFieldDebugGrid::MinSpacing, PhysicsFieldDebugGrid::MaxSpacing)
        : PhysicsFieldDebugSlice::Spacing;
    const float halfExtent = spacing * (count - 1) * 0.5f;
    std::vector<math::Vec3> positions;
    positions.reserve(count * count);
    for (int row = 0; row < count; ++row)
    {
        position[vertical] = slice.Center[vertical] + row * spacing - halfExtent;
        for (int col = 0; col < count; ++col)
        {
            position[horizontal] = slice.Center[horizontal] + col * spacing - halfExtent;
            positions.push_back(position);
        }
    }
    return positions;
}

std::vector<PhysicsFieldDebugScalarSample> SamplePhysicsScalarField(
    const ScalarField& field, const PhysicsFieldDebugSlice& slice)
{
    const auto positions = BuildPhysicsFieldDebugSlicePositions(slice);
    std::vector<PhysicsFieldDebugScalarSample> samples;
    samples.reserve(positions.size());
    for (const auto& position : positions)
    {
        const float value = field.Evaluate(position);
        if (std::isfinite(value))
        {
            samples.push_back({ position, value });
        }
    }
    return samples;
}
}
