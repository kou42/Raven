#pragma once

#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UIFontAtlas.h"

#include <string>

namespace Raven
{

// 明示Sizeを持つ最小の文字表示Widgetです。
// 文字列のMeasure/折り返しはPhase 2で追加し、現段階では配置済みBounds内へGlyphを発行します。
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

protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    Ref<UIFontAtlas> m_Font;
    std::string m_Text;
    math::Vec4 m_TextColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    float m_BaselineOffset = 16.0f;
    float m_LineHeight = 20.0f;
};

} // namespace Raven
