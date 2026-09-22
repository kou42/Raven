#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Core/UIContext.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Raven
{

void UILabel::SetFont(const Ref<UIFontAtlas>& font)
{
    m_DPIFontCache = nullptr;
    m_DPIFontPending = false;
    m_DPIFontFailure = UIFontAtlasBuildFailure::None;
    m_DPIFontFailedScaleStep = 0u;
    m_Font = font;
    m_FontRasterScale = 1.0f;
    InvalidateMeasure();
}

void UILabel::SetFontDPI(const Ref<UIFontAtlas>& font, float rasterScale)
{
    if (std::isfinite(rasterScale) == false || rasterScale <= 0.0f)
    {
        return;
    }
    m_Font = font;
    m_FontRasterScale = rasterScale;
    m_ScaleGlyphsWithDPI = true;
    InvalidateMeasure();
}

void UILabel::BindDPIFontCache(const Ref<UIFontAtlasDPICache>& cache,
    std::string fontPath, std::vector<std::uint32_t> codepoints,
    const UIFontAtlasBuildOptions& options)
{
    m_DPIFontCache = cache;
    m_DPIFontPath = std::move(fontPath);
    m_DPIFontCodepoints = std::move(codepoints);
    m_DPIFontOptions = options;
    m_DPIFontFailure = UIFontAtlasBuildFailure::None;
    m_DPIFontFailedScaleStep = 0u;
    const UIContext* context = GetContext();
    SwitchCachedDPIFont(context != nullptr ? context->GetEffectiveScaleY() : 1.0f);
}

void UILabel::RetryDPIFont()
{
    if (m_DPIFontCache == nullptr)
    {
        return;
    }
    m_DPIFontFailure = UIFontAtlasBuildFailure::None;
    m_DPIFontFailedScaleStep = 0u;
    m_DPIFontPending = true;
}

void UILabel::SwitchCachedDPIFont(float effectiveScale)
{
    if (m_DPIFontCache == nullptr)
    {
        m_DPIFontPending = false;
        m_DPIFontFailure = UIFontAtlasBuildFailure::None;
        return;
    }
    UIFontAtlasBuildOptions resolved{};
    float requestedRasterScale = 1.0f;
    const bool valid = UIFontAtlasBuilder::ResolveDPIOptions(
        m_DPIFontOptions, effectiveScale, resolved, requestedRasterScale);
    const std::uint32_t requestedStep = valid == true
        ? static_cast<std::uint32_t>(requestedRasterScale * 8.0f) : 0u;
    if (m_DPIFontFailure != UIFontAtlasBuildFailure::None &&
        requestedStep != m_DPIFontFailedScaleStep)
    {
        // 別のDPI倍率なら前回の失敗を引き継がず、新しいAtlasを試します。
        m_DPIFontFailure = UIFontAtlasBuildFailure::None;
        m_DPIFontFailedScaleStep = 0u;
    }
    float rasterScale = 1.0f;
    const Ref<UIFontAtlas> atlas = m_DPIFontCache->Find(m_DPIFontPath,
        m_DPIFontCodepoints, m_DPIFontOptions, effectiveScale, rasterScale);
    if (atlas == nullptr)
    {
        // DPI callbackにはGPU Contextの保証がないため、ここでは生成しません。
        // 旧Atlasを維持し、次の安全な描画準備段階でRefreshDPIFontを呼べるよう通知します。
        m_DPIFontPending = true;
        return;
    }
    m_DPIFontPending = false;
    m_DPIFontFailure = UIFontAtlasBuildFailure::None;
    m_DPIFontFailedScaleStep = 0u;
    if (m_Font != atlas || m_FontRasterScale != rasterScale || m_ScaleGlyphsWithDPI == false)
    {
        SetFontDPI(atlas, rasterScale);
    }
}

bool UILabel::RefreshDPIFont()
{
    if (m_DPIFontCache == nullptr)
    {
        return false;
    }
    const UIContext* context = GetContext();
    const float scaleY = context != nullptr ? context->GetEffectiveScaleY() : 1.0f;
    float rasterScale = 1.0f;
    UIFontAtlasBuildFailure failure = UIFontAtlasBuildFailure::None;
    const Ref<UIFontAtlas> atlas = m_DPIFontCache->GetOrBuild(m_DPIFontPath,
        m_DPIFontCodepoints, m_DPIFontOptions, scaleY, rasterScale, &failure);
    if (atlas == nullptr)
    {
        m_DPIFontPending = true;
        m_DPIFontFailure = failure;
        UIFontAtlasBuildOptions resolved{};
        float requestedRasterScale = 1.0f;
        const bool valid = UIFontAtlasBuilder::ResolveDPIOptions(
            m_DPIFontOptions, scaleY, resolved, requestedRasterScale);
        m_DPIFontFailedScaleStep = valid == true
            ? static_cast<std::uint32_t>(requestedRasterScale * 8.0f) : 0u;
        return false;
    }
    m_DPIFontPending = false;
    m_DPIFontFailure = UIFontAtlasBuildFailure::None;
    m_DPIFontFailedScaleStep = 0u;
    if (m_Font != atlas || m_FontRasterScale != rasterScale || m_ScaleGlyphsWithDPI == false)
    {
        SetFontDPI(atlas, rasterScale);
    }
    return true;
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
    if (m_UseBaselineOffsetDIP == true || m_UseLineHeightDIP == true || m_ScaleGlyphsWithDPI == true)
    {
        InvalidateMeasure();
    }
    SwitchCachedDPIFont(scaleY);
}

void UILabel::OnDPIScaleChanged()
{
    RefreshDIPTypography();
    const UIContext* context = GetContext();
    SwitchCachedDPIFont(context != nullptr ? context->GetEffectiveScaleY() : 1.0f);
    if (m_ScaleGlyphsWithDPI == true)
    {
        InvalidateMeasure();
    }
}

void UILabel::SetScaleGlyphsWithDPI(bool enabled)
{
    if (m_ScaleGlyphsWithDPI == enabled)
    {
        return;
    }
    m_ScaleGlyphsWithDPI = enabled;
    InvalidateMeasure();
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
    if (m_ScaleGlyphsWithDPI == true)
    {
        const UIContext* context = GetContext();
        const float scaleX = context != nullptr ? context->GetEffectiveScaleX() : 1.0f;
        const float scaleY = context != nullptr ? context->GetEffectiveScaleY() : 1.0f;
        // AtlasのRasterize倍率と現在のDPI倍率の差分だけ拡大し、二重Scaleを防ぎます。
        options.GlyphScale = math::Vec2(scaleX / m_FontRasterScale, scaleY / m_FontRasterScale);
    }
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
    if (m_ScaleGlyphsWithDPI == true)
    {
        const UIContext* context = GetContext();
        const float scaleX = context != nullptr ? context->GetEffectiveScaleX() : 1.0f;
        const float scaleY = context != nullptr ? context->GetEffectiveScaleY() : 1.0f;
        // Measureと同じ補正を描画にも適用し、高解像度Atlasの二重拡大を防ぎます。
        options.GlyphScale = math::Vec2(scaleX / m_FontRasterScale, scaleY / m_FontRasterScale);
    }
    options.MaxWidth = GetSize().x;
    options.Wrap = m_WrapMode;
    options.Alignment = m_TextAlignment;
    m_Font->AppendText(drawList, m_Text, baseline, options, ApplyVisualColor(GetTextColor()));
}

} // namespace Raven
