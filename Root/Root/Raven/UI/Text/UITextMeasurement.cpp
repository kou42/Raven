#include "Raven/UI/Text/UITextMeasurement.h"

#include "Raven/UI/Text/UIUtf8.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Raven
{

UITextMetrics UITextMeasurement::Measure(const UIFontAtlas& font, std::string_view text, float lineHeight)
{
    UITextMetrics result{};
    if (text.empty() || std::isfinite(lineHeight) == false || lineHeight <= 0.0f)
    {
        return result;
    }

    result.Ascent = font.GetAscent();
    result.Descent = font.GetDescent();
    result.LineCount = 1u;
    float lineWidth = 0.0f;
    std::size_t offset = 0u;
    std::uint32_t codepoint = 0u;
    while (UIUtf8::DecodeNext(text, offset, codepoint))
    {
        if (codepoint == static_cast<std::uint32_t>('\r'))
        {
            // AppendTextと同様、CRは無視し、LFのみ行を進めます。
            continue;
        }
        if (codepoint == static_cast<std::uint32_t>('\n'))
        {
            result.Width = std::max(result.Width, lineWidth);
            lineWidth = 0.0f;
            if (result.LineCount < std::numeric_limits<std::uint32_t>::max())
            {
                ++result.LineCount;
            }
            continue;
        }

        const UIGlyphMetrics* glyph = font.FindGlyph(codepoint);
        if (glyph == nullptr)
        {
            glyph = font.FindGlyph(UIUtf8::ReplacementCharacter);
        }
        if (glyph == nullptr)
        {
            glyph = font.FindGlyph(static_cast<std::uint32_t>('?'));
        }
        if (glyph != nullptr)
        {
            lineWidth += glyph->Advance;
        }
    }

    result.Width = std::max(result.Width, lineWidth);
    result.Height = static_cast<float>(result.LineCount) * lineHeight;
    result.Size = math::Vec2(result.Width, result.Height);
    return result;
}

} // namespace Raven
