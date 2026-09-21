#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Raven
{

// Fontの単位系は任意の設計単位です。FontSize / UnitsPerEmでpixelへ変換します。
// Glyphの実際の形状やTextureは所有せず、描画Backendに依存しない配置情報だけを扱います。
struct FontGlyphMetrics
{
    float Advance = 0.0f;
    float BearingX = 0.0f;
    float BearingY = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;
};

struct FontMetrics
{
    float UnitsPerEm = 1.0f;
    float Ascender = 0.0f;
    float Descender = 0.0f; // baselineより下は負値
    float LineHeight = 0.0f;
    FontGlyphMetrics FallbackGlyph{};
    std::unordered_map<char32_t, FontGlyphMetrics> Glyphs;

    const FontGlyphMetrics& GetGlyph(char32_t codepoint) const
    {
        const auto found = Glyphs.find(codepoint);
        if (found != Glyphs.end())
        {
            return found->second;
        }
        return FallbackGlyph;
    }
};

struct TextLayoutOptions
{
    float FontSize = 16.0f;
    float LineSpacing = 1.0f;
};

struct TextMetrics
{
    float Width = 0.0f;      // 最大行のadvance幅（末尾空白を含む）
    float Height = 0.0f;     // 行ボックス全体の高さ
    float Ascender = 0.0f;
    float Descender = 0.0f;
    float LineHeight = 0.0f;
    uint32_t LineCount = 0;
};

struct PositionedGlyph
{
    char32_t Codepoint = 0;
    std::size_t ByteOffset = 0;
    uint32_t LineIndex = 0;
    float X = 0.0f; // 左上原点、右が正
    float Y = 0.0f; // 左上原点、下が正
    float Width = 0.0f;
    float Height = 0.0f;
};

struct TextLayoutResult
{
    TextMetrics Metrics;
    std::vector<PositionedGlyph> Glyphs;
};

class TextLayout
{
public:
    // UTF-8の不正なsequenceはU+FFFDに置換し、必ず1byte以上進めます。
    // grapheme cluster、shaping、kerningは未対応。各Unicode codepointを独立に配置します。
    static TextLayoutResult Calculate(
        std::string_view text, const FontMetrics& font,
        const TextLayoutOptions& options = {})
    {
        TextLayoutResult result;
        if (text.empty() == true || font.UnitsPerEm <= 0.0f ||
            options.FontSize <= 0.0f || options.LineSpacing <= 0.0f)
        {
            return result;
        }

        const float scale = options.FontSize / font.UnitsPerEm;
        const float ascender = font.Ascender * scale;
        const float lineHeight = font.LineHeight * scale * options.LineSpacing;
        if (lineHeight <= 0.0f)
        {
            return result;
        }

        result.Metrics.Ascender = ascender;
        result.Metrics.Descender = font.Descender * scale;
        result.Metrics.LineHeight = lineHeight;
        result.Metrics.LineCount = 1;

        float penX = 0.0f;
        float baselineY = ascender;
        std::size_t offset = 0;
        while (offset < text.size())
        {
            const std::size_t byteOffset = offset;
            const char32_t codepoint = DecodeUtf8(text, offset);
            if (codepoint == U'\r')
            {
                // CRLFは1回の改行として扱い、単独CRも改行として扱います。
                if (offset < text.size() && text[offset] == '\n')
                {
                    ++offset;
                }
            }
            if (codepoint == U'\r' || codepoint == U'\n')
            {
                result.Metrics.Width = std::max(result.Metrics.Width, penX);
                penX = 0.0f;
                baselineY += lineHeight;
                ++result.Metrics.LineCount;
                continue;
            }

            const FontGlyphMetrics& glyph = font.GetGlyph(codepoint);
            result.Glyphs.push_back({
                codepoint, byteOffset, result.Metrics.LineCount - 1,
                penX + glyph.BearingX * scale,
                baselineY - glyph.BearingY * scale,
                glyph.Width * scale, glyph.Height * scale
            });
            penX += glyph.Advance * scale;
        }

        result.Metrics.Width = std::max(result.Metrics.Width, penX);
        result.Metrics.Height = font.LineHeight * scale +
            (result.Metrics.LineCount - 1) * lineHeight;
        return result;
    }

    static TextMetrics Measure(
        std::string_view text, const FontMetrics& font,
        const TextLayoutOptions& options = {})
    {
        return Calculate(text, font, options).Metrics;
    }

private:
    static char32_t DecodeUtf8(std::string_view text, std::size_t& offset)
    {
        const auto first = static_cast<unsigned char>(text[offset++]);
        if (first < 0x80u)
        {
            return first;
        }

        uint32_t value = 0;
        uint32_t minimum = 0;
        std::size_t continuationCount = 0;
        if (first >= 0xC2u && first <= 0xDFu)
        {
            value = first & 0x1Fu;
            minimum = 0x80u;
            continuationCount = 1;
        }
        else if (first >= 0xE0u && first <= 0xEFu)
        {
            value = first & 0x0Fu;
            minimum = 0x800u;
            continuationCount = 2;
        }
        else if (first >= 0xF0u && first <= 0xF4u)
        {
            value = first & 0x07u;
            minimum = 0x10000u;
            continuationCount = 3;
        }
        else
        {
            return U'\uFFFD';
        }

        // 途中で失敗した場合は、正常な先頭byteを次の文字として再処理できるよう
        // 不正なcontinuation byteを消費しません。
        for (std::size_t i = 0; i < continuationCount; ++i)
        {
            if (offset >= text.size())
            {
                return U'\uFFFD';
            }
            const auto next = static_cast<unsigned char>(text[offset]);
            if ((next & 0xC0u) != 0x80u)
            {
                return U'\uFFFD';
            }
            value = (value << 6) | (next & 0x3Fu);
            ++offset;
        }

        if (value < minimum || value > 0x10FFFFu ||
            (value >= 0xD800u && value <= 0xDFFFu))
        {
            return U'\uFFFD';
        }
        return static_cast<char32_t>(value);
    }
};

} // namespace Raven
