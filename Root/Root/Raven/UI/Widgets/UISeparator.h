#pragma once

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Core/UIElement.h"

namespace Raven
{

// EditorのSection区切りに使う、入力を受け付けない装飾Widgetです。
// レイアウト幅は親が決定し、太さだけをWidget側で指定します。
class UISeparator final : public UIElement
{
public:
    UISeparator()
    {
        SetHitTestVisible(false);
        SetPreferredSize(UIEditorStyle{}.SeparatorSize);
    }

    void SetColor(const math::Vec4& color)
    {
        m_Color = color;
        m_ColorOverride = true;
    }
    math::Vec4 GetColor() const
    {
        const UIContext* context = GetContext();
        if (m_ColorOverride == false && context != nullptr)
        {
            return context->GetTheme().Editor.SeparatorColor;
        }
        return m_Color;
    }

protected:
    void OnContextChanged(UIContext* previous, UIContext* current) override
    {
        (void)previous;
        if (current != nullptr)
        {
            SetPreferredSize(current->GetTheme().Editor.SeparatorSize);
        }
    }

    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override
    {
        const math::Vec2 size = GetSize();
        if (size.x <= 0.0f || size.y <= 0.0f)
        {
            return;
        }
        drawList.AddRect(absolutePosition,
            math::Vec2(absolutePosition.x + size.x, absolutePosition.y + size.y),
            ApplyVisualColor(GetColor()));
    }

private:
    math::Vec4 m_Color{ 0.40f, 0.42f, 0.48f, 1.0f };
    bool m_ColorOverride = false;
};

} // namespace Raven
