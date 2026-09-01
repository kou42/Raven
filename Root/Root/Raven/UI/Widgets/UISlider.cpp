#include "Raven/UI/Widgets/UISlider.h"
#include "Raven/UI/Core/UIContext.h"

#include <algorithm>
#include <utility>

namespace Raven
{

void UISlider::SetRange(float minimum, float maximum)
{
    if (maximum < minimum)
    {
        std::swap(minimum, maximum);
    }

    m_Minimum = minimum;
    m_Maximum = maximum;
    SetValue(m_Value);
}

void UISlider::SetValue(float value)
{
    const float clampedValue = std::clamp(value, m_Minimum, m_Maximum);
    if (clampedValue == m_Value)
    {
        return;
    }

    m_Value = clampedValue;
    if (m_OnValueChanged != nullptr)
    {
        m_OnValueChanged(m_Value);
    }
}

void UISlider::SetOnValueChanged(ValueChangedHandler handler)
{
    m_OnValueChanged = std::move(handler);
}

void UISlider::SetTrackColor(const math::Vec4& color)
{
    m_TrackColor = color;
}

void UISlider::SetFillColor(const math::Vec4& color)
{
    m_FillColor = color;
}

void UISlider::SetThumbColor(const math::Vec4& color)
{
    m_ThumbColor = color;
}

void UISlider::SetHoveredThumbColor(const math::Vec4& color)
{
    m_HoveredThumbColor = color;
}

void UISlider::SetActiveThumbColor(const math::Vec4& color)
{
    m_ActiveThumbColor = color;
}

float UISlider::GetMinimum() const { return m_Minimum; }
float UISlider::GetMaximum() const { return m_Maximum; }
float UISlider::GetValue() const { return m_Value; }
bool UISlider::IsDragging() const { return m_Dragging; }

void UISlider::OnMouseEvent(UIMouseEvent& event)
{
    // Bubble中のParentとして受け取ったEventではDragを開始しません。
    // Capture中はUIContextがTargetをこのSliderへ固定するため、Element外のMove/Upもこの条件を満たします。
    if (event.Target != this)
    {
        return;
    }

    if (event.Type == UIMouseEventType::Cancel)
    {
        // Mouse Upが届かずContext側からCaptureを強制終了された場合も、
        // Drag状態だけがWidgetへ残らないよう必ず局所状態を掃除します。
        m_Dragging = false;
        event.Handled = true;
        return;
    }

    if (event.Type == UIMouseEventType::Down && event.Button == UIMouseButton::Left)
    {
        if (event.Context != nullptr && event.Context->CaptureMouse(this) == true)
        {
            m_Dragging = true;
            UpdateValueFromScreenPosition(event.ScreenPosition);
            event.Handled = true;
        }
        return;
    }

    if (event.Type == UIMouseEventType::Move && m_Dragging == true)
    {
        // Captureが何らかの理由で解除されている場合は、別Widgetの入力まで消費しないよう更新を止めます。
        if (event.Context != nullptr && event.Context->HasMouseCapture(this) == true)
        {
            UpdateValueFromScreenPosition(event.ScreenPosition);
            event.Handled = true;
        }
        else
        {
            // Cancel Eventを経由しない古い/外部コードからCaptureだけ解除された場合にも備えた防御です。
            m_Dragging = false;
        }
        return;
    }

    if (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left && m_Dragging == true)
    {
        // MouseUp位置も最終値へ反映してからCaptureを解放します。
        // UIContext側のPressed解除はRouting完了後なので、このHandler中ではPressed Visualも維持されています。
        UpdateValueFromScreenPosition(event.ScreenPosition);
        if (event.Context != nullptr)
        {
            event.Context->ReleaseMouseCapture(this);
        }

        m_Dragging = false;
        event.Handled = true;
    }
}

void UISlider::OnBuildDrawList(
    UIDrawList& drawList,
    const math::Vec2& absolutePosition) const
{
    const math::Vec2& size = GetSize();
    if (size.x <= 0.0f || size.y <= 0.0f)
    {
        return;
    }

    const float normalizedValue = GetNormalizedValue();
    const float trackHeight = std::min(m_TrackHeight, size.y);
    const float trackTop = absolutePosition.y + (size.y - trackHeight) * 0.5f;
    const float trackBottom = trackTop + trackHeight;
    const float trackRight = absolutePosition.x + size.x;
    const float fillRight = absolutePosition.x + size.x * normalizedValue;

    drawList.AddRect(
        math::Vec2(absolutePosition.x, trackTop),
        math::Vec2(trackRight, trackBottom),
        ApplyVisualColor(m_TrackColor));

    if (fillRight > absolutePosition.x)
    {
        drawList.AddRect(
            math::Vec2(absolutePosition.x, trackTop),
            math::Vec2(fillRight, trackBottom),
            ApplyVisualColor(m_FillColor));
    }

    const float thumbWidth = std::min(m_ThumbWidth, size.x);
    const float thumbCenterX = absolutePosition.x + size.x * normalizedValue;
    const float thumbLeft = std::clamp(
        thumbCenterX - thumbWidth * 0.5f,
        absolutePosition.x,
        absolutePosition.x + size.x - thumbWidth);

    const math::Vec4* thumbColor = &m_ThumbColor;
    if (m_Dragging == true || IsPressed() == true)
    {
        thumbColor = &m_ActiveThumbColor;
    }
    else if (IsHovered() == true)
    {
        thumbColor = &m_HoveredThumbColor;
    }

    drawList.AddRect(
        math::Vec2(thumbLeft, absolutePosition.y),
        math::Vec2(thumbLeft + thumbWidth, absolutePosition.y + size.y),
        ApplyVisualColor(*thumbColor));
}

void UISlider::UpdateValueFromScreenPosition(const math::Vec2& screenPosition)
{
    const math::Vec2& size = GetSize();
    if (size.x <= 0.0f)
    {
        return;
    }

    const math::Vec2 absolutePosition = GetAbsolutePosition();
    const float normalizedValue = std::clamp(
        (screenPosition.x - absolutePosition.x) / size.x,
        0.0f,
        1.0f);

    SetValue(m_Minimum + (m_Maximum - m_Minimum) * normalizedValue);
}

math::Vec2 UISlider::GetAbsolutePosition() const
{
    // Arrange後のPositionはParent-local座標なので、祖先を辿ってScreen座標へ変換します。
    // SliderをVertical/Horizontal Layout配下へ置いてもDrag値計算がDraw位置と一致するようにします。
    math::Vec2 absolutePosition(0.0f, 0.0f);
    const UIElement* current = this;
    while (current != nullptr)
    {
        const math::Vec2& localPosition = current->GetPosition();
        absolutePosition.x += localPosition.x;
        absolutePosition.y += localPosition.y;
        current = current->GetParent();
    }
    return absolutePosition;
}

float UISlider::GetNormalizedValue() const
{
    const float range = m_Maximum - m_Minimum;
    if (range <= 0.0f)
    {
        return 0.0f;
    }

    return std::clamp((m_Value - m_Minimum) / range, 0.0f, 1.0f);
}

} // namespace Raven
