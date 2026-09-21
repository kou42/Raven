#include "Raven/UI/Text/UIFontAtlas.h"

#include "Raven/UI/Text/UITextLayout.h"

namespace Raven
{

math::Vec2 UIFontAtlas::AppendText(
    UIDrawList& drawList,
    std::string_view text,
    const math::Vec2& baseline,
    float lineHeight,
    const math::Vec4& color) const
{
    const UITextLayoutResult layout = UITextLayout::Build(*this, text, lineHeight);
    for (const UITextLayoutGlyph& glyph : layout.Glyphs)
    {
        AppendGlyph(drawList, glyph.Codepoint, baseline + glyph.Pen, color);
    }
    // 旧APIの戻り値は最終行の絶対Pen位置です。
    return baseline + layout.FinalPen;
}

} // namespace Raven
