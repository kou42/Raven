#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIElement.h"

#include <functional>

namespace Raven
{

// ============================================================================
// UIButton
// ============================================================================
// UIElementのHover / Pressed / Focus状態をVisual Stateへ変換する最小Button Widgetです。
// Clickは左ButtonでこのElement上から押下を開始し、同じElement上で離した場合だけ成立します。
// Keyboard Focus中はEnter / Spaceでも同じClick Handlerを呼び、入力手段によるActionの差を作りません。
class UIButton : public UIElement
{
public:
    using ClickHandler = std::function<void()>;

    UIButton();
    ~UIButton() override;

    void SetNormalColor(const math::Vec4& color);
    void SetHoveredColor(const math::Vec4& color);
    void SetPressedColor(const math::Vec4& color);
    void SetFocusedColor(const math::Vec4& color);
    void SetOnClick(ClickHandler handler);

    const math::Vec4& GetNormalColor() const;
    const math::Vec4& GetHoveredColor() const;
    const math::Vec4& GetPressedColor() const;
    const math::Vec4& GetFocusedColor() const;

protected:
    void OnMouseEvent(UIMouseEvent& event) override;
    void OnKeyEvent(UIKeyEvent& event) override;
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    struct StyleOverrides;
    // 標準色はContextのThemeを共有し、個別SetColorを初めて呼んだInstanceだけが所有します。
    // Hover / Pressed / Focusは従来どおりUIElement、Click HandlerはこのInstanceに保持します。
    Scope<StyleOverrides> m_Overrides;
    ClickHandler m_OnClick;
};

} // namespace Raven
