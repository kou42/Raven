#include "Raven/UI/Rendering/UITessellator.h"

#include "Raven/Assets/TextureAsset.h"

#include <cmath>
#include <numeric>

namespace Raven
{
namespace
{
constexpr uint32_t kCircleSegmentCount = 48u;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kPolygonEpsilon = 0.00001f;

float Cross2D(const math::Vec2& a, const math::Vec2& b, const math::Vec2& c)
{
    return (b.x - a.x) * (c.y - a.y) -
        (b.y - a.y) * (c.x - a.x);
}

float CalculateSignedArea(const std::vector<math::Vec2>& points)
{
    float doubledArea = 0.0f;
    for (std::size_t index = 0u; index < points.size(); ++index)
    {
        const math::Vec2& current = points[index];
        const math::Vec2& next = points[(index + 1u) % points.size()];
        doubledArea += current.x * next.y - next.x * current.y;
    }
    return doubledArea * 0.5f;
}

bool IsPointInsideTriangle(
    const math::Vec2& point,
    const math::Vec2& a,
    const math::Vec2& b,
    const math::Vec2& c)
{
    const float cross0 = Cross2D(a, b, point);
    const float cross1 = Cross2D(b, c, point);
    const float cross2 = Cross2D(c, a, point);
    const bool hasNegative =
        cross0 < -kPolygonEpsilon ||
        cross1 < -kPolygonEpsilon ||
        cross2 < -kPolygonEpsilon;
    const bool hasPositive =
        cross0 > kPolygonEpsilon ||
        cross1 > kPolygonEpsilon ||
        cross2 > kPolygonEpsilon;
    return (hasNegative && hasPositive) == false;
}

bool TriangulatePolygon(
    const std::vector<math::Vec2>& points,
    std::vector<uint32_t>& outIndices)
{
    outIndices.clear();
    if (points.size() < 3u)
    {
        return false;
    }

    const float signedArea = CalculateSignedArea(points);
    if (std::abs(signedArea) <= kPolygonEpsilon)
    {
        return false;
    }

    // Ear Clippingを共通層に置き、BackendごとのConcave Polygon結果差を防ぎます。
    const float windingSign = signedArea > 0.0f ? 1.0f : -1.0f;
    std::vector<uint32_t> remaining(points.size());
    std::iota(remaining.begin(), remaining.end(), 0u);

    while (remaining.size() > 3u)
    {
        bool earFound = false;
        for (std::size_t currentIndex = 0u; currentIndex < remaining.size(); ++currentIndex)
        {
            const std::size_t previousIndex =
                (currentIndex + remaining.size() - 1u) % remaining.size();
            const std::size_t nextIndex = (currentIndex + 1u) % remaining.size();
            const uint32_t previousVertex = remaining[previousIndex];
            const uint32_t currentVertex = remaining[currentIndex];
            const uint32_t nextVertex = remaining[nextIndex];

            if (Cross2D(points[previousVertex], points[currentVertex], points[nextVertex]) *
                windingSign <= kPolygonEpsilon)
            {
                continue;
            }

            bool containsOtherVertex = false;
            for (uint32_t candidateVertex : remaining)
            {
                if (candidateVertex == previousVertex ||
                    candidateVertex == currentVertex ||
                    candidateVertex == nextVertex)
                {
                    continue;
                }
                if (IsPointInsideTriangle(
                    points[candidateVertex], points[previousVertex],
                    points[currentVertex], points[nextVertex]) == true)
                {
                    containsOtherVertex = true;
                    break;
                }
            }
            if (containsOtherVertex == true)
            {
                continue;
            }

            outIndices.push_back(previousVertex);
            outIndices.push_back(currentVertex);
            outIndices.push_back(nextVertex);
            remaining.erase(
                remaining.begin() + static_cast<std::ptrdiff_t>(currentIndex));
            earFound = true;
            break;
        }

        if (earFound == false)
        {
            outIndices.clear();
            return false;
        }
    }

    outIndices.push_back(remaining[0u]);
    outIndices.push_back(remaining[1u]);
    outIndices.push_back(remaining[2u]);
    return true;
}
} // namespace

bool UITessellator::Tessellate(
    const UIDrawList& drawList,
    UITessellatedDrawList& outDrawList)
{
    outDrawList = {};
    outDrawList.Vertices.reserve(drawList.GetCommandCount() * 8u);
    outDrawList.Indices.reserve(drawList.GetCommandCount() * kCircleSegmentCount * 3u);
    outDrawList.Commands.reserve(drawList.GetCommandCount());

    uint32_t vertexBase = 0u;
    for (const UIDrawCommand& command : drawList.GetCommands())
    {
        UITessellatedCommand batch{};
        batch.FirstIndex = static_cast<uint32_t>(outDrawList.Indices.size());
        batch.Clip = command.Clip;
        batch.Texture = command.Texture;
        batch.UseTexture =
            command.Type == UIDrawCommandType::Image &&
            command.Texture != nullptr &&
            command.Texture->IsValid() == true;

        const auto pushVertex = [&outDrawList, &command](
            float x, float y, float u, float v)
        {
            UIVertex vertex{};
            vertex.Position = command.Transform.TransformPoint({ x, y });
            vertex.Color = command.Color;
            vertex.Texcoord = { u, v };
            outDrawList.Vertices.push_back(vertex);
        };

        const float left = command.Rect.Min.x;
        const float top = command.Rect.Min.y;
        const float right = command.Rect.Max.x;
        const float bottom = command.Rect.Max.y;

        if (command.Type == UIDrawCommandType::SolidCircle)
        {
            const float centerX = (left + right) * 0.5f;
            const float centerY = (top + bottom) * 0.5f;
            const float radiusX = (right - left) * 0.5f;
            const float radiusY = (bottom - top) * 0.5f;
            pushVertex(centerX, centerY, 0.5f, 0.5f);
            for (uint32_t segment = 0u; segment < kCircleSegmentCount; ++segment)
            {
                const float angle =
                    kTwoPi * static_cast<float>(segment) /
                    static_cast<float>(kCircleSegmentCount);
                const float cosine = std::cos(angle);
                const float sine = std::sin(angle);
                pushVertex(
                    centerX + cosine * radiusX,
                    centerY + sine * radiusY,
                    0.5f + cosine * 0.5f,
                    0.5f + sine * 0.5f);
            }
            for (uint32_t segment = 0u; segment < kCircleSegmentCount; ++segment)
            {
                outDrawList.Indices.push_back(vertexBase);
                outDrawList.Indices.push_back(vertexBase + 1u + segment);
                outDrawList.Indices.push_back(
                    vertexBase + 1u + ((segment + 1u) % kCircleSegmentCount));
            }
            vertexBase += 1u + kCircleSegmentCount;
        }
        else if (command.Type == UIDrawCommandType::SolidPolygon)
        {
            std::vector<uint32_t> localIndices;
            const bool triangulated =
                TriangulatePolygon(command.Points, localIndices);
            for (const math::Vec2& point : command.Points)
            {
                pushVertex(point.x, point.y, 0.0f, 0.0f);
            }
            if (triangulated == true)
            {
                for (uint32_t localIndex : localIndices)
                {
                    outDrawList.Indices.push_back(vertexBase + localIndex);
                }
            }
            vertexBase += static_cast<uint32_t>(command.Points.size());
        }
        else
        {
            pushVertex(left, top, command.UVMin.x, command.UVMin.y);
            pushVertex(right, top, command.UVMax.x, command.UVMin.y);
            pushVertex(right, bottom, command.UVMax.x, command.UVMax.y);
            pushVertex(left, bottom, command.UVMin.x, command.UVMax.y);
            outDrawList.Indices.insert(outDrawList.Indices.end(), {
                vertexBase + 0u, vertexBase + 1u, vertexBase + 2u,
                vertexBase + 2u, vertexBase + 3u, vertexBase + 0u
            });
            vertexBase += 4u;
        }

        batch.IndexCount =
            static_cast<uint32_t>(outDrawList.Indices.size()) - batch.FirstIndex;
        outDrawList.Commands.push_back(std::move(batch));
    }

    return outDrawList.IsEmpty() == false;
}

} // namespace Raven
