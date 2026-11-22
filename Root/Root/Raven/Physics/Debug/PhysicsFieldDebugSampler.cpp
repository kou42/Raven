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
}
