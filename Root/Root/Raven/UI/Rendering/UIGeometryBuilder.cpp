#include "Raven/UI/Rendering/UIGeometryBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Raven
{
namespace
{
bool SameTexture(const UIRenderBatch& previous, const UITessellatedCommand& command)
{
    if (previous.UseTexture != command.UseTexture)
    {
        return false;
    }
    if (command.UseTexture == false)
    {
        return true;
    }
    const auto& a = previous.TextureView;
    const auto& b = command.TextureView;
    // UV原点はTessellatorが各Vertexへ適用済み。ここではBinding先の同一性だけを比較します。
    return a.GetSourceType() == b.GetSourceType() &&
        a.GetAsset() == b.GetAsset() && a.GetLegacyTexture() == b.GetLegacyTexture() &&
        a.GetRHITexture() == b.GetRHITexture();
}
}

bool UIGeometryBuilder::Build(const UIDrawList& drawList, const math::Vec2& logicalSize,
    uint32_t framebufferWidth, uint32_t framebufferHeight,
    UIRenderGeometry& outGeometry, bool mergeAdjacent)
{
    outGeometry = {};
    if (std::isfinite(logicalSize.x) == false || std::isfinite(logicalSize.y) == false ||
        logicalSize.x <= 0.0f || logicalSize.y <= 0.0f ||
        framebufferWidth == 0u || framebufferHeight == 0u ||
        framebufferWidth > static_cast<uint32_t>((std::numeric_limits<int32_t>::max)()) ||
        framebufferHeight > static_cast<uint32_t>((std::numeric_limits<int32_t>::max)()))
    {
        return false;
    }
    UITessellatedDrawList mesh;
    UITessellator::Tessellate(drawList, mesh);
    // Legacy Buffer APIのbyte/count上限を越えるGeometryを切り詰めて描画しません。
    if (mesh.Vertices.size() > (std::numeric_limits<uint32_t>::max)() / sizeof(UIVertex) ||
        mesh.Indices.size() > (std::numeric_limits<uint32_t>::max)())
    {
        return false;
    }
    UIRenderGeometry result;
    result.SourceCommandCount = drawList.GetCommandCount();
    result.Vertices = std::move(mesh.Vertices);
    result.Indices = std::move(mesh.Indices);
    result.Batches.reserve(mesh.Commands.size());
    const double scaleX = static_cast<double>(framebufferWidth) / logicalSize.x;
    const double scaleY = static_cast<double>(framebufferHeight) / logicalSize.y;
    for (const auto& command : mesh.Commands)
    {
        if (command.IndexCount == 0u)
        {
            continue;
        }
        UIPixelClip clip{0u, 0u, framebufferWidth, framebufferHeight};
        if (command.Clip.Enabled == true)
        {
            const auto& rect = command.Clip.Rect;
            if (std::isfinite(rect.Min.x) == false || std::isfinite(rect.Min.y) == false ||
                std::isfinite(rect.Max.x) == false || std::isfinite(rect.Max.y) == false)
            {
                return false;
            }
            const double left = std::clamp(static_cast<double>(rect.Min.x), 0.0, static_cast<double>(logicalSize.x));
            const double top = std::clamp(static_cast<double>(rect.Min.y), 0.0, static_cast<double>(logicalSize.y));
            const double right = std::clamp(static_cast<double>(rect.Max.x), 0.0, static_cast<double>(logicalSize.x));
            const double bottom = std::clamp(static_cast<double>(rect.Max.y), 0.0, static_cast<double>(logicalSize.y));
            if (right <= left || bottom <= top)
            {
                continue;
            }
            // 各端を外側へ丸め、非整数倍率でも同じ境界Pixelを両Backendで保持します。
            // 最後に実Framebuffer端へClampし、ceilで1 Pixel越えることも防ぎます。
            const auto x = static_cast<uint32_t>(std::clamp(std::floor(left * scaleX), 0.0, static_cast<double>(framebufferWidth)));
            const auto y = static_cast<uint32_t>(std::clamp(std::floor(top * scaleY), 0.0, static_cast<double>(framebufferHeight)));
            const auto endX = static_cast<uint32_t>(std::clamp(std::ceil(right * scaleX), 0.0, static_cast<double>(framebufferWidth)));
            const auto endY = static_cast<uint32_t>(std::clamp(std::ceil(bottom * scaleY), 0.0, static_cast<double>(framebufferHeight)));
            clip = {x, y, endX - x, endY - y};
            if (clip.Width == 0u || clip.Height == 0u)
            {
                continue;
            }
        }
        ++result.DrawableCommandCount;
        if (mergeAdjacent == true && result.Batches.empty() == false)
        {
            auto& previous = result.Batches.back();
            // 透明描画のPainter順序を維持し、連続Index・同じClip/Bindingの隣接範囲だけを結合します。
            if (previous.FirstIndex + previous.IndexCount == command.FirstIndex &&
                previous.Clip == clip && SameTexture(previous, command) == true)
            {
                previous.IndexCount += command.IndexCount;
                continue;
            }
        }
        result.Batches.push_back({command.FirstIndex, command.IndexCount, clip, command.TextureView, command.UseTexture});
    }
    outGeometry = std::move(result);
    return true;
}
} // namespace Raven
