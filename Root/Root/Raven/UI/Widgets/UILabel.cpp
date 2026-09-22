#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Core/UIContext.h"

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
    m_TextColorOverride = true;
}

const math::Vec4& UILabel::GetTextColor() const
{
    const UIContext* context = GetContext();
    return m_TextColorOverride == false && context != nullptr
        ? context->GetTheme().Label.TextColor : m_TextColor;
}

void UILabel::SetBaselineOffset(float offset)
{
    m_UseBaselineOffsetDIP = false;
    m_BaselineOffset = offset;
    InvalidateMeasure();
}

void UILabel::SetLineHeight(float height)
{
    if (std::isfinite(height) && height > 0.0f)
    {
        m_UseLineHeightDIP = false;
        m_LineHeight = height;
        InvalidateMeasure();
    }
}

void UILabel::SetBaselineOffsetDIP(float offset)
{
    if (std::isfinite(offset) == false)
    {
        return;
    }
    m_BaselineOffsetDIP = offset;
    m_UseBaselineOffsetDIP = true;
    RefreshDIPTypography();
}

void UILabel::SetLineHeightDIP(float height)
{
    if (std::isfinite(height) == false || height <= 0.0f)
    {
        return;
    }
    m_LineHeightDIP = height;
    m_UseLineHeightDIP = true;
    RefreshDIPTypography();
}

void UILabel::RefreshDIPTypography()
{
    const UIContext* context = GetContext();
    const float scaleY = context != nullptr ? context->GetEffectiveScaleY() : 1.0f;
    if (m_UseBaselineOffsetDIP == true)
    {
        m_BaselineOffset = m_BaselineOffsetDIP * scaleY;
    }
    if (m_UseLineHeightDIP == true)
    {
        m_LineHeight = m_LineHeightDIP * scaleY;
    }
    if (m_UseBaselineOffsetDIP == true || m_UseLineHeightDIP == true)
    {
        InvalidateMeasure();
    }
}

void UILabel::OnContextChanged(UIContext* previous, UIContext* current)
{
    static_cast<void>(previous);
    static_cast<void>(current);
    // SetContextRecursiveはこの通知後にContextを差し替えるため、倍率は新Contextから直接取得します。
    const float scaleY = current != nullptr ? current->GetEffectiveScaleY() : 1.0f;
    if (m_UseBaselineOffsetDIP == true)
    {
        m_BaselineOffset = m_BaselineOffsetDIP * scaleY;
    }
    if (m_UseLineHeightDIP == true)
    {
        m_LineHeight = m_LineHeightDIP * scaleY;
    }
    if (m_UseBaselineOffsetDIP == true || m_UseLineHeightDIP == true)
    {
        InvalidateMeasure();
    }
}

void UILabel::OnDPIScaleChanged()
{
    RefreshDIPTypography();
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
    m_Font->AppendText(drawList, m_Text, baseline, options, ApplyVisualColor(GetTextColor()));
}

} // namespace Raven
