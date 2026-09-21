#pragma once

#include "Raven/Math/MathVector.h"

#include <cstdint>

namespace Raven
{

// SizeはPen Advanceに基づく論理領域です。GlyphのBitmap外接矩形とは異なります。
// 空文字列は0行、末尾改行は空の最終行を含めて数えます。
struct UITextMetrics
{
    math::Vec2 Size{};
    float Width = 0.0f;
    float Height = 0.0f;
    float Ascent = 0.0f;
    float Descent = 0.0f;
    std::uint32_t LineCount = 0u;
};

} // namespace Raven
