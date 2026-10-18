#include "Raven/Editor/EditorViewportGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Raven
{

bool TryCalculateEditorViewportRenderTargetExtent(
    const math::Vec2& viewportLogicalSize,
    const math::Vec2& windowLogicalSize,
    const math::Vec2& windowFramebufferPixelSize,
    EditorViewportRenderTargetExtent& outExtent)
{
    outExtent = EditorViewportRenderTargetExtent{};
    if (std::isfinite(viewportLogicalSize.x) == false ||
        std::isfinite(viewportLogicalSize.y) == false ||
        std::isfinite(windowLogicalSize.x) == false ||
        std::isfinite(windowLogicalSize.y) == false ||
        std::isfinite(windowFramebufferPixelSize.x) == false ||
        std::isfinite(windowFramebufferPixelSize.y) == false ||
        viewportLogicalSize.x <= 0.0f || viewportLogicalSize.y <= 0.0f ||
        windowLogicalSize.x <= 0.0f || windowLogicalSize.y <= 0.0f ||
        windowFramebufferPixelSize.x <= 0.0f ||
        windowFramebufferPixelSize.y <= 0.0f)
    {
        return false;
    }

    // UIContextの論理座標へContent Scaleを再適用しません。実Framebuffer / 論理Windowの
    // 比率だけを使うことで、OS DPIと非整数Framebuffer倍率の双方を同じ境界で扱います。
    const double scaleX = static_cast<double>(windowFramebufferPixelSize.x) /
        static_cast<double>(windowLogicalSize.x);
    const double scaleY = static_cast<double>(windowFramebufferPixelSize.y) /
        static_cast<double>(windowLogicalSize.y);
    const double visibleLogicalWidth = std::min(
        static_cast<double>(viewportLogicalSize.x),
        static_cast<double>(windowLogicalSize.x));
    const double visibleLogicalHeight = std::min(
        static_cast<double>(viewportLogicalSize.y),
        static_cast<double>(windowLogicalSize.y));
    const double pixelWidth = std::ceil(visibleLogicalWidth * scaleX);
    const double pixelHeight = std::ceil(visibleLogicalHeight * scaleY);
    const double maximum = static_cast<double>(
        std::numeric_limits<std::uint32_t>::max());
    if (std::isfinite(pixelWidth) == false || std::isfinite(pixelHeight) == false ||
        pixelWidth < 1.0 || pixelHeight < 1.0 ||
        pixelWidth > maximum || pixelHeight > maximum ||
        windowFramebufferPixelSize.x > maximum ||
        windowFramebufferPixelSize.y > maximum)
    {
        return false;
    }

    outExtent.LogicalSize = math::Vec2(
        static_cast<float>(visibleLogicalWidth),
        static_cast<float>(visibleLogicalHeight));
    outExtent.Width = std::min(
        static_cast<std::uint32_t>(pixelWidth),
        static_cast<std::uint32_t>(windowFramebufferPixelSize.x));
    outExtent.Height = std::min(
        static_cast<std::uint32_t>(pixelHeight),
        static_cast<std::uint32_t>(windowFramebufferPixelSize.y));
    return outExtent.Width > 0u && outExtent.Height > 0u;
}

} // namespace Raven
