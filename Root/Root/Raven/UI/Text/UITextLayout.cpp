#include "Raven/UI/Text/UITextLayout.h"

#include "Raven/UI/Text/UIUtf8.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Raven
{

UITextLayoutResult UITextLayout::Build(const UIFontAtlas& font, std::string_view text, float lineHeight)
{
    UITextLayoutOptions options{};
    options.LineHeight = lineHeight;
    return Build(font, text, options);
}

UITextLayoutResult UITextLayout::Build(const UIFontAtlas& font, std::string_view text, const UITextLayoutOptions& options)
{
    UITextLayoutResult result{};
    if (text.empty() || std::isfinite(options.LineHeight) == false || options.LineHeight <= 0.0f)
    {
        return result;
    }

    const bool constrained = std::isfinite(options.MaxWidth) && options.MaxWidth > 0.0f;
    const bool wrap = constrained && options.Wrap == UITextWrapMode::Character;
    result.Metrics.Ascent = font.GetAscent();
    result.Metrics.Descent = font.GetDescent();
    result.Lines.push_back({ 0.0f, 0.0f });

    std::size_t offset = 0u;
    std::uint32_t codepoint = 0u;
    while (UIUtf8::DecodeNext(text, offset, codepoint))
    {
        if (codepoint == static_cast<std::uint32_t>('\r'))
        {
            continue;
        }
        if (codepoint == static_cast<std::uint32_t>('\n'))
        {
            result.Metrics.Width = std::max(result.Metrics.Width, result.Lines.back().Width);
            result.FinalPen.x = 0.0f;
            result.FinalPen.y += options.LineHeight;
            // 末尾改行でも空行を保持します。
            if (result.Lines.size() < std::numeric_limits<std::uint32_t>::max())
            {
                result.Lines.push_back({ 0.0f, result.FinalPen.y });
            }
            continue;
        }

        const UIGlyphMetrics* glyph = font.FindGlyph(codepoint);
        std::uint32_t resolved = codepoint;
        if (glyph == nullptr)
        {
            resolved = UIUtf8::ReplacementCharacter;
            glyph = font.FindGlyph(resolved);
        }
        if (glyph == nullptr)
        {
            resolved = static_cast<std::uint32_t>('?');
            glyph = font.FindGlyph(resolved);
        }
        if (glyph == nullptr)
        {
            continue;
        }

        // 最初のGlyphが制限幅より大きくても空行を増やさず、その行に配置します。
        if (wrap && result.Lines.back().Width > 0.0f &&
            glyph->Advance > options.MaxWidth - result.Lines.back().Width)
        {
            result.Metrics.Width = std::max(result.Metrics.Width, result.Lines.back().Width);
            result.FinalPen.x = 0.0f;
            result.FinalPen.y += options.LineHeight;
            if (result.Lines.size() < std::numeric_limits<std::uint32_t>::max())
            {
                result.Lines.push_back({ 0.0f, result.FinalPen.y });
            }
        }

        result.Glyphs.push_back({ resolved, result.FinalPen });
        result.FinalPen.x += glyph->Advance;
        result.Lines.back().Width = result.FinalPen.x;
    }

    result.Metrics.Width = std::max(result.Metrics.Width, result.Lines.back().Width);
    result.Metrics.LineCount = static_cast<std::uint32_t>(result.Lines.size());
    result.Metrics.Height = static_cast<float>(result.Metrics.LineCount) * options.LineHeight;
    result.Metrics.Size = math::Vec2(result.Metrics.Width, result.Metrics.Height);

    // 幅指定がある場合は指定幅、なければ最長行をAlignmentの基準にします。
    const float alignmentWidth = constrained ? options.MaxWidth : result.Metrics.Width;
    for (std::size_t lineIndex = 0u; lineIndex < result.Lines.size(); ++lineIndex)
    {
        const float remainder = std::max(0.0f, alignmentWidth - result.Lines[lineIndex].Width);
        float shift = 0.0f;
        if (options.Alignment == UITextHorizontalAlignment::Center)
        {
            shift = remainder * 0.5f;
        }
        else if (options.Alignment == UITextHorizontalAlignment::Right)
        {
            shift = remainder;
        }

        // 行のBaselineYは改行ごとに増えるため、Glyphの所属行を順番に判定できます。
        for (UITextLayoutGlyph& glyph : result.Glyphs)
        {
            if (glyph.Pen.y == result.Lines[lineIndex].BaselineY)
            {
                glyph.Pen.x += shift;
            }
        }
        if (lineIndex + 1u == result.Lines.size())
        {
            result.FinalPen.x += shift;
        }
    }
    return result;
}

} // namespace Raven
