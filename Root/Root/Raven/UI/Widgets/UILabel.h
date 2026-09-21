#pragma once

#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UIFontAtlas.h"
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
    const Ref<UIFontAtlas>& GetFont() const;

    void SetText(std::string text);
    const std::string& GetText() const;

    void SetTextColor(const math::Vec4& color);
    const math::Vec4& GetTextColor() const;

    // Element左上から最初のBaselineまでの距離です。
    void SetBaselineOffset(float offset);
    void SetLineHeight(float height);

    void SetWrapMode(UITextWrapMode mode);
    void SetTextAlignment(UITextHorizontalAlignment alignment);
    UITextWrapMode GetWrapMode() const;
    UITextHorizontalAlignment GetTextAlignment() const;

protected:
    math::Vec2 OnMeasureContent() const override;
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    Ref<UIFontAtlas> m_Font;
    std::string m_Text;
    math::Vec4 m_TextColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    float m_BaselineOffset = 16.0f;
    float m_LineHeight = 20.0f;
    UITextWrapMode m_WrapMode = UITextWrapMode::None;
    UITextHorizontalAlignment m_TextAlignment = UITextHorizontalAlignment::Left;
};

} // namespace Raven
