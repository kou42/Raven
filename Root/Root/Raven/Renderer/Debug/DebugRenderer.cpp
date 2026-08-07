#include "Raven/Renderer/Debug/DebugRenderer.h"

#include <algorithm>

namespace Raven
{

std::vector<DebugLine> DebugRenderer::s_Lines;

void DebugRenderer::Clear()
{
    s_Lines.clear();
}

void DebugRenderer::DrawLine(
    const math::Vec3& start,
    const math::Vec3& end,
    const math::Vec3& color)
{
    s_Lines.push_back(DebugLine{ start, end, color });
}

void DebugRenderer::DrawAABB(
    const ph::AABB& bounds,
    const math::Vec3& color)
{
    if (!bounds.IsValid())
    {
        return;
    }

    // AABBの8頂点です。
    //
    //      7 -------- 6
    //     /|         /|
    //    4 -------- 5 |
    //    | |        | |
    //    | 3 -------|-2
    //    |/         |/
    //    0 -------- 1
    //
    // この頂点列から下面4辺、上面4辺、縦4辺の合計12辺を生成します。
    const math::Vec3 p0{ bounds.Min.x, bounds.Min.y, bounds.Min.z };
    const math::Vec3 p1{ bounds.Max.x, bounds.Min.y, bounds.Min.z };
    const math::Vec3 p2{ bounds.Max.x, bounds.Min.y, bounds.Max.z };
    const math::Vec3 p3{ bounds.Min.x, bounds.Min.y, bounds.Max.z };

    const math::Vec3 p4{ bounds.Min.x, bounds.Max.y, bounds.Min.z };
    const math::Vec3 p5{ bounds.Max.x, bounds.Max.y, bounds.Min.z };
    const math::Vec3 p6{ bounds.Max.x, bounds.Max.y, bounds.Max.z };
    const math::Vec3 p7{ bounds.Min.x, bounds.Max.y, bounds.Max.z };

    // bottom
    DrawLine(p0, p1, color);
    DrawLine(p1, p2, color);
    DrawLine(p2, p3, color);
    DrawLine(p3, p0, color);

    // top
    DrawLine(p4, p5, color);
    DrawLine(p5, p6, color);
    DrawLine(p6, p7, color);
    DrawLine(p7, p4, color);

    // vertical
    DrawLine(p0, p4, color);
    DrawLine(p1, p5, color);
    DrawLine(p2, p6, color);
    DrawLine(p3, p7, color);
}

void DebugRenderer::DrawPoint(
    const math::Vec3& position,
    float radius,
    const math::Vec3& color)
{
    const float safeRadius = std::max(radius, 0.0f);

    const math::Vec3 x{ safeRadius, 0.0f, 0.0f };
    const math::Vec3 y{ 0.0f, safeRadius, 0.0f };
    const math::Vec3 z{ 0.0f, 0.0f, safeRadius };

    DrawLine(position - x, position + x, color);
    DrawLine(position - y, position + y, color);
    DrawLine(position - z, position + z, color);
}

const std::vector<DebugLine>& DebugRenderer::GetLines()
{
    return s_Lines;
}

} // namespace Raven
