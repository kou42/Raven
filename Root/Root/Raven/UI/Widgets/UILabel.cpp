#include "Raven/UI/Widgets/UILabel.h"

#include <utility>

namespace Raven
{

void UILabel::SetFont(const Ref<UIFontAtlas>& font)
{
    m_Font = font;
}

const Ref<UIFontAtlas>& UILabel::GetFont() const
{
    return m_Font;
}

void UILabel::SetText(std::string text)
{
    m_Text = std::move(text);
}

const std::string& UILabel::GetText() const
{
    return m_Text;
}

void UILabel::SetTextColor(const math::Vec4& color)
{
    m_TextColor = color;
}

const math::Vec4& UILabel::GetTextColor() const
{
    return m_TextColor;
}

void UILabel::SetBaselineOffset(float offset)
{
    m_BaselineOffset = offset;
}

void UILabel::SetLineHeight(float height)
{
    if (height > 0.0f)
    {
        m_LineHeight = height;
    }
}

void UILabel::SetWrapMode(UITextWrapMode mode)
{
    m_WrapMode = mode;
}

void UILabel::SetTextAlignment(UITextHorizontalAlignment alignment)
{
    m_TextAlignment = alignment;
}

UITextWrapMode UILabel::GetWrapMode() const
{
    return m_WrapMode;
}

UITextHorizontalAlignment UILabel::GetTextAlignment() const
{
    return m_TextAlignment;
}

void UILabel::OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const
{
    if (m_Font == nullptr || m_Font->GetTexture() == nullptr || m_Text.empty())
    {
        return;
    }

    // GlyphのTextureAssetはDrawList Command側でもRef保持され、Widgetが変更されても
    // 当該Frameの描画が終わるまでTextureの寿命が保たれます。
    const math::Vec2 baseline(absolutePosition.x, absolutePosition.y + m_BaselineOffset);
    UITextLayoutOptions options{};
    options.LineHeight = m_LineHeight;
    options.MaxWidth = GetSize().x;
    options.Wrap = m_WrapMode;
    options.Alignment = m_TextAlignment;
    m_Font->AppendText(drawList, m_Text, baseline, options, ApplyVisualColor(m_TextColor));
}

} // namespace Raven
