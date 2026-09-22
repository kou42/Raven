#include "Raven/UI/Text/UIFontAtlas.h"

#include "Raven/UI/Text/UITextLayout.h"

namespace Raven
{

bool UIFontAtlas::AppendGlyphScaled(
    UIDrawList& drawList,
    std::uint32_t codepoint,
    const math::Vec2& pen,
    const math::Vec2& glyphScale,
    const math::Vec4& color) const
{
    const UIGlyphMetrics* glyph = FindGlyph(codepoint);
    if (glyph == nullptr || m_Texture == nullptr ||
        glyphScale.x <= 0.0f || glyphScale.y <= 0.0f)
    {
        return false;
    }
    const math::Vec2 size(
        (glyph->AtlasRect.Max.x - glyph->AtlasRect.Min.x) * glyphScale.x,
        (glyph->AtlasRect.Max.y - glyph->AtlasRect.Min.y) * glyphScale.y);
    if (size.x > 0.0f && size.y > 0.0f)
    {
        const math::Vec2 min(pen.x + glyph->Bearing.x * glyphScale.x,
            pen.y + glyph->Bearing.y * glyphScale.y);
        const math::Vec2 uvMin(glyph->AtlasRect.Min.x / static_cast<float>(m_Width),
            glyph->AtlasRect.Min.y / static_cast<float>(m_Height));
        const math::Vec2 uvMax(glyph->AtlasRect.Max.x / static_cast<float>(m_Width),
            glyph->AtlasRect.Max.y / static_cast<float>(m_Height));
        drawList.AddGlyph(min, min + size, m_Texture, uvMin, uvMax, color);
    }
    return true;
}

math::Vec2 UIFontAtlas::AppendText(
    UIDrawList& drawList,
    std::string_view text,
    const math::Vec2& baseline,
    float lineHeight,
    const math::Vec4& color) const
{
    UITextLayoutOptions options{};
    options.LineHeight = lineHeight;
    return AppendText(drawList, text, baseline, options, color);
}

math::Vec2 UIFontAtlas::AppendText(
    UIDrawList& drawList,
    std::string_view text,
    const math::Vec2& baseline,
    const UITextLayoutOptions& options,
    const math::Vec4& color) const
{
    const UITextLayoutResult layout = UITextLayout::Build(*this, text, options);
    for (const UITextLayoutGlyph& glyph : layout.Glyphs)
    {
        // Measureと同じGlyphScaleでBearing/Quadを拡大し、PenはLayoutで拡大済みです。
        AppendGlyphScaled(drawList, glyph.Codepoint, baseline + glyph.Pen, options.GlyphScale, color);
    }
    // 旧APIの戻り値は最終行の絶対Pen位置です。
    return baseline + layout.FinalPen;
}

} // namespace Raven
