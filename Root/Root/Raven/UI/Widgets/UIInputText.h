#pragma once

#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UIFontAtlas.h"
#include "Raven/UI/Text/UITextEditBuffer.h"
#include "Raven/UI/Text/UIIMEComposition.h"

#include <functional>
#include <string>

namespace Raven
{

// 単一行の入力Widget。文字編集はUITextEditBufferへ委譲し、描画とFocusのみ担当します。
class UIInputText final : public UIElement
{
public:
    using ChangeHandler = std::function<void(const std::string&)>;
    using ClipboardReader = std::function<std::string()>;
    using ClipboardWriter = std::function<void(const std::string&)>;
    using InputFilter = std::function<bool(const std::string&)>;
    using SubmitHandler = std::function<void()>;
    using StepHandler = std::function<void(bool)>;

    UIInputText() { SetFocusable(true); SetClipChildren(true); SetClipSelf(true); }
    void SetFont(const Ref<UIFontAtlas>& font) { m_Font = font; m_ScrollX = 0.0f; InvalidateMeasure(); }
    void SetText(std::string_view text);
    const std::string& GetText() const { return m_Edit.GetText(); }
    const UITextEditBuffer& GetEditBuffer() const { return m_Edit; }
    const UIIMEComposition& GetIMEComposition() const { return m_Composition; }
    bool HasActiveIMEComposition() const override { return m_Composition.IsActive(); }
    // OSのIME候補WindowをCaret直下へ配置するためのGLFW論理画面座標です。
    math::Vec2 GetIMECaretScreenPosition() const;
    void SetOnChange(ChangeHandler handler) { m_OnChange = std::move(handler); }
    void SetClipboard(ClipboardReader reader, ClipboardWriter writer)
    {
        m_ReadClipboard = std::move(reader);
        m_WriteClipboard = std::move(writer);
    }
    void SetInputFilter(InputFilter filter) { m_InputFilter = std::move(filter); }
    void SetOnSubmit(SubmitHandler handler) { m_OnSubmit = std::move(handler); }
    void SetOnFocusLost(SubmitHandler handler) { m_OnFocusLost = std::move(handler); }
    void SetOnStep(StepHandler handler) { m_OnStep = std::move(handler); }
    void SetTextColor(const math::Vec4& color) { m_TextColor = color; m_TextColorOverride = true; }

protected:
    math::Vec2 OnMeasureContent() const override;
    void OnMouseEvent(UIMouseEvent& event) override;
    void OnKeyEvent(UIKeyEvent& event) override;
    void OnCharacterEvent(UICharacterEvent& event) override;
    void OnIMEEvent(UIIMEEvent& event) override;
    void OnFocusChanged(bool focused) override;
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    float CursorX(std::size_t index) const;
    float TextCursorX(std::string_view text, std::size_t index) const;
    std::string GetDisplayText() const;
    std::size_t GetDisplayCursor() const;
    void EnsureCursorVisible() const;
    std::size_t HitCursor(float localX) const;
    void NotifyChanged();
    bool InsertFiltered(std::string_view text);
    bool CommitIMEText(std::string_view text);

    Ref<UIFontAtlas> m_Font;
    UITextEditBuffer m_Edit;
    UIIMEComposition m_Composition;
    ChangeHandler m_OnChange;
    ClipboardReader m_ReadClipboard;
    ClipboardWriter m_WriteClipboard;
    InputFilter m_InputFilter;
    SubmitHandler m_OnSubmit;
    SubmitHandler m_OnFocusLost;
    StepHandler m_OnStep;
    math::Vec4 m_TextColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    // 明示的なText色はTheme切替後も維持します。
    bool m_TextColorOverride = false;
    bool m_Selecting = false;
    mutable float m_ScrollX = 0.0f;
    float m_Padding = 6.0f;
    float m_Baseline = 22.0f;
    float m_Height = 30.0f;
};

} // namespace Raven
