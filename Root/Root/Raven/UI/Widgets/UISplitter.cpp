#include "Raven/UI/Widgets/UISplitter.h"
#include "Raven/UI/Core/UIContext.h"

#include <utility>

namespace Raven
{

void UISplitter::SetOrientation(UISplitterOrientation orientation)
{
    m_Orientation = orientation;
}

void UISplitter::SetOnDragDelta(DragDeltaHandler handler)
{
    m_OnDragDelta = std::move(handler);
}

void UISplitter::SetNormalColor(const math::Vec4& color)
{
    m_NormalColor = color;
}

void UISplitter::SetHoveredColor(const math::Vec4& color)
{
    m_HoveredColor = color;
}

void UISplitter::SetActiveColor(const math::Vec4& color)
{
    m_ActiveColor = color;
}

UISplitterOrientation UISplitter::GetOrientation() const
{
    return m_Orientation;
}

bool UISplitter::IsDragging() const
{
    return m_Dragging;
}

void UISplitter::OnMouseEvent(UIMouseEvent& event)
{
    // Bubbleで親Splitterまで誤反応しないよう、Drag開始/継続対象が自分自身の場合だけ処理します。
    // Capture中はUIContextがTargetをこのElementへ固定するため、Handle外のMove/Upも同じ経路へ届きます。
    if (event.Target != this)
    {
        return;
    }

    if (event.Type == UIMouseEventType::Cancel)
    {
        // Focus Lost等でMouse Upを失った場合でも、ContextからのCancelでDrag状態を確実に終了します。
        m_Dragging = false;
        event.Handled = true;
        return;
    }

    if (event.Type == UIMouseEventType::Down && event.Button == UIMouseButton::Left)
    {
        if (event.Context != nullptr && event.Context->CaptureMouse(this) == true)
        {
            m_LastAxisPosition = GetAxisPosition(event.ScreenPosition);
            m_Dragging = true;
            event.Handled = true;
        }
        return;
    }

    if (event.Type == UIMouseEventType::Move && m_Dragging == true)
    {
        if (event.Context != nullptr && event.Context->HasMouseCapture(this) == true)
        {
            UpdateDrag(event.ScreenPosition);
            event.Handled = true;
        }
        else
        {
            // Cancel通知を経由しない外部解除にも耐え、別WidgetのMoveを以降消費しないよう局所状態を戻します。
            m_Dragging = false;
        }
        return;
    }

    if (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left && m_Dragging == true)
    {
        // 最後のMouseUp位置まで差分へ反映してからCaptureを正常解放します。
        UpdateDrag(event.ScreenPosition);
        if (event.Context != nullptr)
        {
            event.Context->ReleaseMouseCapture(this);
        }

        m_Dragging = false;
        event.Handled = true;
    }
}

void UISplitter::OnBuildDrawList(
    UIDrawList& drawList,
    const math::Vec2& absolutePosition) const
{
    const math::Vec2& size = GetSize();
    if (size.x <= 0.0f || size.y <= 0.0f)
    {
        return;
    }

    const math::Vec4* color = &m_NormalColor;
    if (m_Dragging == true || IsPressed() == true)
    {
        color = &m_ActiveColor;
    }
    else if (IsHovered() == true)
    {
        color = &m_HoveredColor;
    }

    drawList.AddRect(
        absolutePosition,
        math::Vec2(absolutePosition.x + size.x, absolutePosition.y + size.y),
        *color);
}

float UISplitter::GetAxisPosition(const math::Vec2& screenPosition) const
{
    if (m_Orientation == UISplitterOrientation::Vertical)
    {
        return screenPosition.x;
    }

    return screenPosition.y;
}

void UISplitter::UpdateDrag(const math::Vec2& screenPosition)
{
    const float currentAxisPosition = GetAxisPosition(screenPosition);
    const float delta = currentAxisPosition - m_LastAxisPosition;
    m_LastAxisPosition = currentAxisPosition;

    // Pointer Eventが同一座標で複数回届く場合に無意味なLayout更新を発生させないよう、
    // 実際に移動したframeだけConsumerへ差分を通知します。
    if (delta != 0.0f && m_OnDragDelta != nullptr)
    {
        m_OnDragDelta(delta);
    }
}

} // namespace Raven
