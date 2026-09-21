#include "Raven/UI/Widgets/UILabel.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Raven
{

void UILabel::SetFont(const Ref<UIFontAtlas>& font)
{
    m_Font = font;
    InvalidateMeasure();
}

const Ref<UIFontAtlas>& UILabel::GetFont() const
{
    return m_Font;
}

void UILabel::SetText(std::string text)
{
    m_Text = std::move(text);
    InvalidateMeasure();
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
    InvalidateMeasure();
}

void UILabel::SetLineHeight(float height)
{
    if (std::isfinite(height) && height > 0.0f)
    {
        m_LineHeight = height;
        InvalidateMeasure();
    }
}

void UILabel::SetWrapMode(UITextWrapMode mode)
{
    m_WrapMode = mode;
    InvalidateMeasure();
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

math::Vec2 UILabel::OnMeasureContent() const
{
    return MeasureText(GetPreferredSize().x);
}

math::Vec2 UILabel::OnMeasureContentForWidth(float availableWidth) const
{
    return MeasureText(availableWidth);
}

math::Vec2 UILabel::MeasureText(float maxWidth) const
{
    if (m_Font == nullptr || m_Text.empty())
    {
        return math::Vec2(0.0f, 0.0f);
    }

    UITextLayoutOptions options{};
    options.LineHeight = m_LineHeight;
    // 初回MeasureはPreferred幅、Stretch時の再Measureは親から確定した幅を使います。
    options.MaxWidth = maxWidth;
    options.Wrap = m_WrapMode;
    const UITextMetrics metrics = UITextLayout::Build(*m_Font, m_Text, options).Metrics;
    const float lastBaseline = (metrics.LineCount > 0u)
        ? static_cast<float>(metrics.LineCount - 1u) * m_LineHeight : 0.0f;
    const float glyphBottom = m_BaselineOffset + lastBaseline +
        std::max(0.0f, -metrics.Descent);
    return math::Vec2(metrics.Width, std::max(metrics.Height, glyphBottom));
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
