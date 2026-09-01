#include "Raven/UI/Widgets/UIPanel.h"

namespace Raven
{

void UIPanel::SetBackgroundColor(const math::Vec4& color)
{
    m_BackgroundColor = color;
}

const math::Vec4& UIPanel::GetBackgroundColor() const
{
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
        ApplyVisualColor(m_BackgroundColor));
}

} // namespace Raven
