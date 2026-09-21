#pragma once

#include "Raven/UI/Text/UIFontAtlas.h"
#include "Raven/UI/Text/UITextMetrics.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Raven
{

enum class UITextWrapMode
{
    None,
    Character
};

enum class UITextHorizontalAlignment
{
    Left,
    Center,
    Right
};

struct UITextLayoutOptions
{
    float LineHeight = 20.0f;
    float MaxWidth = 0.0f; // 0以下は幅制限なし
    UITextWrapMode Wrap = UITextWrapMode::None;
    UITextHorizontalAlignment Alignment = UITextHorizontalAlignment::Left;
};

// Penは入力Baselineからの相対位置です。CodepointはFallback解決後の値です。
struct UITextLayoutGlyph
{
    std::uint32_t Codepoint = 0u;
    math::Vec2 Pen{};
};

struct UITextLayoutLine
{
    float Width = 0.0f;
    float BaselineY = 0.0f;
};

struct UITextLayoutResult
{
    UITextMetrics Metrics{};
    std::vector<UITextLayoutGlyph> Glyphs;
    std::vector<UITextLayoutLine> Lines;
    math::Vec2 FinalPen{};
};

// 描画とMeasurementの共通配置計算。折り返し・Kerningは後続Phaseで拡張します。
class UITextLayout
{
public:
    static UITextLayoutResult Build(const UIFontAtlas& font, std::string_view text, float lineHeight);
    static UITextLayoutResult Build(const UIFontAtlas& font, std::string_view text, const UITextLayoutOptions& options);
};

} // namespace Raven
