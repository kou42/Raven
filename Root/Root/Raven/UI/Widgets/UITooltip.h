#pragma once

#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UIFontAtlas.h"

#include <string>
#include <utility>

namespace Raven
{

// UIContext専用の非操作型Overlay。表示・Hover待機・寿命はContext側が管理します。
class UITooltip final : public UIElement
{
public:
    UITooltip()
    {
        SetHitTestVisible(false);
        SetAffectsParentMeasure(false);
        SetVisible(false);
    }

    void SetContent(std::string text, const Ref<UIFontAtlas>& font)
    {
        m_Text = std::move(text);
        m_Font = font;
        // 文字数からの概算幅。Atlasに登録済みのGlyph Advanceを使う精密Measureは後続対応。
        float width = 0.0f;
        for (unsigned char character : m_Text)
        {
            if (character < 0x80u)
            {
                const UIGlyphMetrics* glyph = m_Font != nullptr ? m_Font->FindGlyph(character) : nullptr;
                width += glyph != nullptr ? glyph->Advance : 12.0f;
            }
            else
            {
                width += 12.0f;
            }
        }
        SetSize(math::Vec2(std::max(40.0f, width + 16.0f), 32.0f));
    }

    const std::string& GetText() const { return m_Text; }

protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& position) const override
    {
        drawList.AddRect(position, position + GetSize(),
            ApplyVisualColor(math::Vec4(0.10f, 0.11f, 0.15f, 0.96f)));
        if (m_Font != nullptr)
        {
            m_Font->AppendText(drawList, m_Text, position + math::Vec2(8.0f, 23.0f),
                28.0f, ApplyVisualColor(math::Vec4(1.0f, 1.0f, 1.0f, 1.0f)));
        }
    }

private:
    std::string m_Text;
    Ref<UIFontAtlas> m_Font;
};

} // namespace Raven
