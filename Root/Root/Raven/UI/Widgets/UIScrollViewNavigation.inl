#pragma once

namespace Raven
{

inline void UIScrollView::ScrollTo(const math::Vec2& offset)
{
    SetScrollOffset(offset);
}

inline bool UIScrollView::EnsureVisible(const UIElement* element, float margin)
{
    if (element == nullptr || m_Content == nullptr)
    {
        return false;
    }

    // 対象がContent自身、またはContent配下のDescendantであることを確認しながら、
    // Scroll Offsetに依存しないContent Local座標へ矩形を変換します。
    math::Vec2 elementMin(0.0f, 0.0f);
    const UIElement* current = element;
    while (current != m_Content)
    {
        const UIElement* parent = current->GetParent();
        if (parent == nullptr)
        {
            return false;
        }

        elementMin.x += current->GetPosition().x;
        elementMin.y += current->GetPosition().y;
        current = parent;
    }

    // Layout直後だけでなくMeasure後にも利用できるよう、Arrange済みSizeだけに依存しません。
    const math::Vec2& arranged = element->GetSize();
    const math::Vec2& desired = element->GetDesiredSize();
    const math::Vec2& preferred = element->GetPreferredSize();
    const math::Vec2 elementSize(
        std::max(arranged.x, std::max(desired.x, preferred.x)),
        std::max(arranged.y, std::max(desired.y, preferred.y)));
    const math::Vec2 elementMax(elementMin.x + elementSize.x, elementMin.y + elementSize.y);
    return EnsureVisible(elementMin, elementMax, margin);
}

inline bool UIScrollView::EnsureVisible(const math::Vec2& contentMin, const math::Vec2& contentMax, float margin)
{
    const math::Vec2 viewportSize = ResolveViewportSize();
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
    {
        return false;
    }

    const float safeMargin = std::max(0.0f, margin);
    // MarginがViewport半分を超えると可視範囲が反転するため、各軸で安全な上限へClampします。
    const float marginX = std::min(safeMargin, viewportSize.x * 0.5f);
    const float marginY = std::min(safeMargin, viewportSize.y * 0.5f);
    const math::Vec2 rectMin(std::min(contentMin.x, contentMax.x), std::min(contentMin.y, contentMax.y));
    const math::Vec2 rectMax(std::max(contentMin.x, contentMax.x), std::max(contentMin.y, contentMax.y));
    math::Vec2 requested = m_ScrollOffset;

    // 矩形そのものがViewportより大きい軸では両端を同時に収められないため、先頭側を優先します。
    // 通常サイズでは現在見えている軸を動かさず、指定Marginを含めて必要な最小距離だけScrollします。
    const float rectWidth = rectMax.x - rectMin.x;
    const float visibleMinX = m_ScrollOffset.x + marginX;
    const float visibleMaxX = m_ScrollOffset.x + viewportSize.x - marginX;
    if (rectWidth > viewportSize.x)
    {
        requested.x = rectMin.x - marginX;
    }
    else if (rectMin.x < visibleMinX)
    {
        requested.x = rectMin.x - marginX;
    }
    else if (rectMax.x > visibleMaxX)
    {
        requested.x = rectMax.x - viewportSize.x + marginX;
    }

    const float rectHeight = rectMax.y - rectMin.y;
    const float visibleMinY = m_ScrollOffset.y + marginY;
    const float visibleMaxY = m_ScrollOffset.y + viewportSize.y - marginY;
    if (rectHeight > viewportSize.y)
    {
        requested.y = rectMin.y - marginY;
    }
    else if (rectMin.y < visibleMinY)
    {
        requested.y = rectMin.y - marginY;
    }
    else if (rectMax.y > visibleMaxY)
    {
        requested.y = rectMax.y - viewportSize.y + marginY;
    }

    const math::Vec2 previous = m_ScrollOffset;
    SetScrollOffset(requested);
    return m_ScrollOffset != previous;
}

} // namespace Raven
