#include "Raven/UI/Widgets/UICircle.h"

namespace Raven
{

void UICircle::SetFillColor(const math::Vec4& color)
{
    m_FillColor = color;
}

const math::Vec4& UICircle::GetFillColor() const
{
    return m_FillColor;
}

void UICircle::OnBuildDrawList(
    UIDrawList& drawList,
    const math::Vec2& absolutePosition) const
{
    const math::Vec2& size = GetSize();

    drawList.AddCircle(
        absolutePosition,
        math::Vec2(
            absolutePosition.x + size.x,
            absolutePosition.y + size.y),
        ApplyVisualColor(m_FillColor));
}

} // namespace Raven
