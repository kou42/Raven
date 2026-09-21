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
    UITextLayoutResult result{};
    if (text.empty() || std::isfinite(lineHeight) == false || lineHeight <= 0.0f)
    {
        return result;
    }

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
            result.FinalPen.y += lineHeight;
            // 最終行が空でも行として保持し、描画Penと論理高さを一致させます。
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
        if (glyph != nullptr)
        {
            result.Glyphs.push_back({ resolved, result.FinalPen });
            result.FinalPen.x += glyph->Advance;
            result.Lines.back().Width = result.FinalPen.x;
        }
    }

    result.Metrics.Width = std::max(result.Metrics.Width, result.Lines.back().Width);
    result.Metrics.LineCount = static_cast<std::uint32_t>(result.Lines.size());
    result.Metrics.Height = static_cast<float>(result.Metrics.LineCount) * lineHeight;
    result.Metrics.Size = math::Vec2(result.Metrics.Width, result.Metrics.Height);
    return result;
}

} // namespace Raven
