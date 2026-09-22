#include "Raven/UI/Widgets/UIPanel.h"
#include "Raven/UI/Core/UIContext.h"

namespace Raven
{

void UIPanel::SetBackgroundColor(const math::Vec4& color)
{
    m_BackgroundColor = color;
    m_BackgroundColorOverride = true;
}

const math::Vec4& UIPanel::GetBackgroundColor() const
{
    const UIContext* context = GetContext();
    if (m_BackgroundColorOverride == false && context != nullptr)
    {
        return context->GetTheme().Panel.BackgroundColor;
    }
    return m_BackgroundColor;
}

void UIPanel::OnBuildDrawList(
    UIDrawList& drawList,
    const math::Vec2& absolutePosition) const
{
    const math::Vec2& size = GetSize();

    drawList.AddRect(
        absolutePosition,
        math::Vec2(
            absolutePosition.x + size.x,
            absolutePosition.y + size.y),
        ApplyVisualColor(GetBackgroundColor()));
}

} // namespace Raven
