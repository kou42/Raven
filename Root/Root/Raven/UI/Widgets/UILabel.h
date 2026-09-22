#pragma once

#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UIFontAtlas.h"
#include "Raven/UI/Text/UIFontAtlasBuilder.h"
#include "Raven/UI/Text/UITextLayout.h"

#include <string>

namespace Raven
{

// 明示Sizeを持つ文字表示Widgetです。折り返し・水平Alignmentは配置済み幅を使います。
// Fontと文字列から固有SizeをMeasureします。折り返し時はPreferredSize.xを幅制約に使用します。
class UILabel final : public UIElement
{
public:
    void SetFont(const Ref<UIFontAtlas>& font);
    // DPI用に再RasterizeしたAtlasを指定し、表示倍率との差分だけQuadを補正します。
    void SetFontDPI(const Ref<UIFontAtlas>& font, float rasterScale);
    const Ref<UIFontAtlas>& GetFont() const;
    // GPU生成は明示Refresh時だけ実施。DPI通知ではCache済みAtlasを自動切替します。
    void BindDPIFontCache(const Ref<UIFontAtlasDPICache>& cache,
        std::string fontPath, std::vector<std::uint32_t> codepoints,
        const UIFontAtlasBuildOptions& options);
    bool RefreshDPIFont(); // 有効なGPU Context上で呼び出してください。
    bool IsDPIFontPending() const { return m_DPIFontPending; }
    bool HasPendingDPIFont() const override { return m_DPIFontPending; }
    bool RefreshPendingDPIFont() override { return m_DPIFontPending == true && RefreshDPIFont(); }

    void SetText(std::string text);
    const std::string& GetText() const;

    void SetTextColor(const math::Vec4& color);
    const math::Vec4& GetTextColor() const;

    // Element左上から最初のBaselineまでの距離です。
    void SetBaselineOffset(float offset);
    void SetLineHeight(float height);
    // 行間とBaselineをDIP指定します。Font AtlasのGlyph自体の拡大は行いません。
    void SetBaselineOffsetDIP(float offset);
    void SetLineHeightDIP(float height);
    float GetBaselineOffset() const { return m_BaselineOffset; }
    float GetLineHeight() const { return m_LineHeight; }
    // 既存の等倍描画は維持し、明示的に有効化したLabelだけGlyphをDPI倍率で拡大します。
    void SetScaleGlyphsWithDPI(bool enabled);
    bool GetScaleGlyphsWithDPI() const { return m_ScaleGlyphsWithDPI; }

    void SetWrapMode(UITextWrapMode mode);
    void SetTextAlignment(UITextHorizontalAlignment alignment);
    UITextWrapMode GetWrapMode() const;
    UITextHorizontalAlignment GetTextAlignment() const;

protected:
    math::Vec2 OnMeasureContent() const override;
    math::Vec2 OnMeasureContentForWidth(float availableWidth) const override;
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;
    void OnContextChanged(UIContext* previous, UIContext* current) override;
    void OnDPIScaleChanged() override;

private:
    math::Vec2 MeasureText(float maxWidth) const;
    void RefreshDIPTypography();
    void SwitchCachedDPIFont(float effectiveScale);

    Ref<UIFontAtlas> m_Font;
    std::string m_Text;
    math::Vec4 m_TextColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool m_TextColorOverride = false;
    float m_BaselineOffset = 16.0f;
    float m_LineHeight = 20.0f;
    float m_BaselineOffsetDIP = 16.0f;
    float m_LineHeightDIP = 20.0f;
    bool m_UseBaselineOffsetDIP = false;
    bool m_UseLineHeightDIP = false;
    bool m_ScaleGlyphsWithDPI = false;
    float m_FontRasterScale = 1.0f;
    Ref<UIFontAtlasDPICache> m_DPIFontCache;
    std::string m_DPIFontPath;
    std::vector<std::uint32_t> m_DPIFontCodepoints;
    UIFontAtlasBuildOptions m_DPIFontOptions{};
    bool m_DPIFontPending = false;
    UITextWrapMode m_WrapMode = UITextWrapMode::None;
    UITextHorizontalAlignment m_TextAlignment = UITextHorizontalAlignment::Left;
};

} // namespace Raven
