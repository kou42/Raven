#pragma once

#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UIFontAtlas.h"
#include "Raven/UI/Text/UITextEditBuffer.h"

#include <functional>
#include <string>

namespace Raven
{

// 単一行の入力Widget。文字編集はUITextEditBufferへ委譲し、描画とFocusのみ担当します。
class UIInputText final : public UIElement
{
public:
    using ChangeHandler = std::function<void(const std::string&)>;

    UIInputText() { SetFocusable(true); SetClipChildren(true); }
    void SetFont(const Ref<UIFontAtlas>& font) { m_Font = font; InvalidateMeasure(); }
    void SetText(std::string_view text) { m_Edit.SetText(text); InvalidateMeasure(); }
    const std::string& GetText() const { return m_Edit.GetText(); }
    const UITextEditBuffer& GetEditBuffer() const { return m_Edit; }
    void SetOnChange(ChangeHandler handler) { m_OnChange = std::move(handler); }
    void SetTextColor(const math::Vec4& color) { m_TextColor = color; }

protected:
    math::Vec2 OnMeasureContent() const override;
    void OnMouseEvent(UIMouseEvent& event) override;
    void OnKeyEvent(UIKeyEvent& event) override;
    void OnCharacterEvent(UICharacterEvent& event) override;
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    float CursorX(std::size_t index) const;
    std::size_t HitCursor(float localX) const;
    void NotifyChanged();

    Ref<UIFontAtlas> m_Font;
    UITextEditBuffer m_Edit;
    ChangeHandler m_OnChange;
    math::Vec4 m_TextColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool m_Selecting = false;
    float m_Padding = 6.0f;
    float m_Baseline = 22.0f;
    float m_Height = 30.0f;
};

} // namespace Raven
