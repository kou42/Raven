#pragma once

#include "Raven/Math/MathVector.h"

#include <cstdint>

namespace Raven
{

// Editor Viewportで混同しやすい3つの座標系を明示的に分離したResize結果です。
// LogicalSizeはRaven UIの論理座標、FramebufferPixelSizeはOS Windowの実Pixel密度を
// 適用した表示領域、RenderTarget Pixelは現段階ではFramebuffer Pixelと1対1にします。
struct EditorViewportRenderTargetExtent
{
    math::Vec2 LogicalSize{};
    std::uint32_t Width = 0u;
    std::uint32_t Height = 0u;
};

// Raven UI上のViewport表示領域をRenderTarget Pixelへ変換します。
// Window最小化中や0サイズではfalseを返し、呼び出し側が既存GPU Resourceを保持できるようにします。
bool TryCalculateEditorViewportRenderTargetExtent(
    const math::Vec2& viewportLogicalSize,
    const math::Vec2& windowLogicalSize,
    const math::Vec2& windowFramebufferPixelSize,
    EditorViewportRenderTargetExtent& outExtent);

} // namespace Raven
