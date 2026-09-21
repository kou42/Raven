#include "Raven/UI/Text/UITextLayout.h"

#include "Raven/UI/Text/UIUtf8.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Raven
{
namespace
{
// ASCIIの英数字とアンダースコアを一語として扱います。日本語は文字単位で折り返します。
bool IsWordCodepoint(std::uint32_t codepoint)
{
    return (codepoint >= 'A' && codepoint <= 'Z') ||
        (codepoint >= 'a' && codepoint <= 'z') ||
        (codepoint >= '0' && codepoint <= '9') || codepoint == '_';
}

const UIGlyphMetrics* ResolveGlyph(const UIFontAtlas& font, std::uint32_t codepoint)
{
    const UIGlyphMetrics* glyph = font.FindGlyph(codepoint);
    if (glyph == nullptr)
    {
        glyph = font.FindGlyph(UIUtf8::ReplacementCharacter);
    }
    if (glyph == nullptr)
    {
        glyph = font.FindGlyph(static_cast<std::uint32_t>('?'));
    }
    return glyph;
}
} // namespace

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
    const bool wrap = constrained && options.Wrap != UITextWrapMode::None;
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

        // 単語先頭で残りのAdvanceを先読みし、収まる単語は途中で分割しません。
        // 制限幅より長い単語はCharacter WrapへFallbackして無限の折り返しを防ぎます。
        if (wrap && options.Wrap == UITextWrapMode::Word && IsWordCodepoint(codepoint) &&
            (offset <= 1u || IsWordCodepoint(static_cast<unsigned char>(text[offset - 2u])) == false))
        {
            std::size_t lookahead = offset;
            std::uint32_t next = 0u;
            float wordWidth = 0.0f;
            const UIGlyphMetrics* first = ResolveGlyph(font, codepoint);
            if (first != nullptr)
            {
                wordWidth += first->Advance;
            }
            while (lookahead < text.size())
            {
                const std::size_t previous = lookahead;
                if (UIUtf8::DecodeNext(text, lookahead, next) == false ||
                    IsWordCodepoint(next) == false)
                {
                    break;
                }
                const UIGlyphMetrics* nextGlyph = ResolveGlyph(font, next);
                if (nextGlyph != nullptr)
                {
                    wordWidth += nextGlyph->Advance;
                }
                if (lookahead <= previous)
                {
                    break;
                }
            }
            if (result.Lines.back().Width > 0.0f &&
                wordWidth <= options.MaxWidth &&
                wordWidth > options.MaxWidth - result.Lines.back().Width)
            {
                // 折り返しで行末に残るASCIIスペースは表示幅にも含めません。
                while (result.Glyphs.empty() == false &&
                    result.Glyphs.back().Pen.y == result.FinalPen.y &&
                    result.Glyphs.back().Codepoint == static_cast<std::uint32_t>(' '))
                {
                    const UIGlyphMetrics* space = font.FindGlyph(static_cast<std::uint32_t>(' '));
                    if (space == nullptr)
                    {
                        break;
                    }
                    result.FinalPen.x -= space->Advance;
                    result.Glyphs.pop_back();
                    result.Lines.back().Width = result.FinalPen.x;
                }
                result.Metrics.Width = std::max(result.Metrics.Width, result.Lines.back().Width);
                result.FinalPen.x = 0.0f;
                result.FinalPen.y += options.LineHeight;
                result.Lines.push_back({ 0.0f, result.FinalPen.y });
            }
        }

        const UIGlyphMetrics* glyph = ResolveGlyph(font, codepoint);
        std::uint32_t resolved = codepoint;
        if (font.FindGlyph(resolved) == nullptr)
        {
            resolved = UIUtf8::ReplacementCharacter;
            if (font.FindGlyph(resolved) == nullptr)
            {
                resolved = static_cast<std::uint32_t>('?');
            }
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
    std::size_t glyphIndex = 0u;
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

        // Glyphは行順に格納されているため、一度だけ走査します。
        while (glyphIndex < result.Glyphs.size() &&
            result.Glyphs[glyphIndex].Pen.y == result.Lines[lineIndex].BaselineY)
        {
            result.Glyphs[glyphIndex].Pen.x += shift;
            ++glyphIndex;
        }
        if (lineIndex + 1u == result.Lines.size())
        {
            result.FinalPen.x += shift;
        }
    }
    return result;
}

} // namespace Raven
