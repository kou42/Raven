#include "Raven/UI/Widgets/UIScrollView.h"
#include "Raven/UI/Core/UIContext.h"

#include <algorithm>
#include <cmath>

namespace Raven
{

class UIScrollBarVisual final : public UIElement
{
public:
    explicit UIScrollBarVisual(bool vertical)
        : m_Vertical(vertical)
    {
    }

    void SetThumbGeometry(float start, float length)
    {
        m_ThumbStart = std::max(0.0f, start);
        m_ThumbLength = std::max(0.0f, length);
    }

    float GetThumbStart() const { return m_ThumbStart; }
    float GetThumbLength() const { return m_ThumbLength; }

    void SetColors(const math::Vec4& trackColor, const math::Vec4& thumbColor)
    {
        m_TrackColor = trackColor;
        m_ThumbColor = thumbColor;

        // Theme側からThumb色を変更してもHover / Pressedだけ固定色へ飛ばないよう、
        // 状態色は現在のThumb色を基準に明度だけを上げて生成します。
        m_ThumbHoverColor = math::Vec4(
            std::clamp(thumbColor.x + 0.10f, 0.0f, 1.0f),
            std::clamp(thumbColor.y + 0.10f, 0.0f, 1.0f),
            std::clamp(thumbColor.z + 0.10f, 0.0f, 1.0f),
            thumbColor.w);
        m_ThumbPressedColor = math::Vec4(
            std::clamp(thumbColor.x + 0.20f, 0.0f, 1.0f),
            std::clamp(thumbColor.y + 0.20f, 0.0f, 1.0f),
            std::clamp(thumbColor.z + 0.20f, 0.0f, 1.0f),
            thumbColor.w);
    }

    void SetThumbPressed(bool value)
    {
        m_ThumbPressed = value;
    }

protected:
    void OnMouseEvent(UIMouseEvent& event) override
    {
        // Hover状態自体はUIContextが管理しますが、Scrollbar Element全体とThumbの範囲は一致しません。
        // そのためPointerのScrollbar Local座標だけを保持し、描画時にThumb上かどうかを判定します。
        if (event.Type == UIMouseEventType::Cancel)
        {
            m_HasPointerPosition = false;
            return;
        }

        math::Vec2 localPosition;
        if (TryScreenToLocalPosition(event.ScreenPosition, localPosition) == false)
        {
            m_HasPointerPosition = false;
            return;
        }

        m_PointerPosition = m_Vertical == true ? localPosition.y : localPosition.x;
        m_HasPointerPosition = true;
    }

    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override
    {
        const math::Vec2& size = GetSize();
        drawList.AddRect(absolutePosition, math::Vec2(absolutePosition.x + size.x, absolutePosition.y + size.y), ApplyVisualColor(m_TrackColor));
        if (m_ThumbLength <= 0.0f)
        {
            return;
        }

        math::Vec2 thumbMin = absolutePosition;
        math::Vec2 thumbMax(absolutePosition.x + size.x, absolutePosition.y + size.y);
        if (m_Vertical == true)
        {
            thumbMin.y += m_ThumbStart;
            thumbMax.y = thumbMin.y + m_ThumbLength;
        }
        else
        {
            thumbMin.x += m_ThumbStart;
            thumbMax.x = thumbMin.x + m_ThumbLength;
        }

        math::Vec4 thumbColor = m_ThumbColor;
        if (m_ThumbPressed == true)
        {
            thumbColor = m_ThumbPressedColor;
        }
        else if (IsPointerOverThumb() == true)
        {
            thumbColor = m_ThumbHoverColor;
        }
        drawList.AddRect(thumbMin, thumbMax, ApplyVisualColor(thumbColor));
    }

private:
    bool IsPointerOverThumb() const
    {
        if (IsHovered() == false || m_HasPointerPosition == false || m_ThumbLength <= 0.0f)
        {
            return false;
        }

        const float thumbEnd = m_ThumbStart + m_ThumbLength;
        return m_PointerPosition >= m_ThumbStart && m_PointerPosition < thumbEnd;
    }

private:
    bool m_Vertical = false;
    float m_ThumbStart = 0.0f;
    float m_ThumbLength = 0.0f;
    float m_PointerPosition = 0.0f;
    bool m_HasPointerPosition = false;
    bool m_ThumbPressed = false;
    math::Vec4 m_TrackColor{ 0.06f, 0.07f, 0.09f, 0.75f };
    math::Vec4 m_ThumbColor{ 0.42f, 0.45f, 0.52f, 0.95f };
    math::Vec4 m_ThumbHoverColor{ 0.52f, 0.55f, 0.62f, 0.95f };
    math::Vec4 m_ThumbPressedColor{ 0.62f, 0.65f, 0.72f, 0.95f };
};

UIScrollView::UIScrollView()
{
    SetClipChildren(true);
    SetLayoutMode(UILayoutMode::Absolute);
    CreateScrollBarElements();
}

UIElement* UIScrollView::SetContent(Scope<UIElement> content)
{
    if (m_DragAxis != ScrollBarDragAxis::None)
    {
        UIContext* context = GetContext();
        if (context != nullptr && context->HasMouseCapture(this) == true)
        {
            context->ReleaseMouseCapture(this);
        }
        EndThumbDrag();
    }

    ClearChildren();
    m_Content = nullptr;
    m_VerticalScrollBar = nullptr;
    m_HorizontalScrollBar = nullptr;
    m_ScrollOffset = math::Vec2(0.0f, 0.0f);

    if (content != nullptr)
    {
        // Contentは自身のMeasureを通常通り行いますが、その大きさをScrollViewのDesiredSizeへ逆流させません。
        // これによりViewportは親Layout/PreferredSizeで決まり、Contentだけが独立してOverflowできます。
        content->SetAffectsParentMeasure(false);
        content->SetPosition(math::Vec2(0.0f, 0.0f));
        m_Content = AddChild(std::move(content));
    }

    CreateScrollBarElements();
    SyncScrollBars();
    return m_Content;
}

UIElement* UIScrollView::GetContent() { return m_Content; }
const UIElement* UIScrollView::GetContent() const { return m_Content; }

void UIScrollView::SetScrollOffset(const math::Vec2& value)
{
    const math::Vec2 maxOffset = GetMaxScrollOffset();
    const math::Vec2 clamped(std::clamp(value.x, 0.0f, maxOffset.x), std::clamp(value.y, 0.0f, maxOffset.y));
    if (clamped != m_ScrollOffset)
    {
        m_ScrollOffset = clamped;
        ApplyContentPosition();
    }
    SyncScrollBars();
}

const math::Vec2& UIScrollView::GetScrollOffset() const { return m_ScrollOffset; }

math::Vec2 UIScrollView::GetMaxScrollOffset() const
{
    const math::Vec2 viewportSize = ResolveViewportSize();
    const math::Vec2 contentSize = ResolveContentSize();
    return math::Vec2(std::max(0.0f, contentSize.x - viewportSize.x), std::max(0.0f, contentSize.y - viewportSize.y));
}

void UIScrollView::RefreshScrollRange() { SyncScrollBars(); }
void UIScrollView::SetWheelScrollStep(float value) { m_WheelScrollStep = std::max(0.0f, value); }
float UIScrollView::GetWheelScrollStep() const { return m_WheelScrollStep; }

void UIScrollView::SetVerticalScrollBarEnabled(bool value)
{
    if (value == false && m_DragAxis == ScrollBarDragAxis::Vertical)
    {
        UIContext* context = GetContext();
        if (context != nullptr && context->HasMouseCapture(this) == true) { context->ReleaseMouseCapture(this); }
        EndThumbDrag();
    }
    m_VerticalScrollBarEnabled = value;
    SyncScrollBars();
}

bool UIScrollView::IsVerticalScrollBarEnabled() const { return m_VerticalScrollBarEnabled; }
bool UIScrollView::IsVerticalScrollBarVisible() const { return m_VerticalScrollBarVisible; }

void UIScrollView::SetHorizontalScrollBarEnabled(bool value)
{
    if (value == false && m_DragAxis == ScrollBarDragAxis::Horizontal)
    {
        UIContext* context = GetContext();
        if (context != nullptr && context->HasMouseCapture(this) == true) { context->ReleaseMouseCapture(this); }
        EndThumbDrag();
    }
    m_HorizontalScrollBarEnabled = value;
    SyncScrollBars();
}

bool UIScrollView::IsHorizontalScrollBarEnabled() const { return m_HorizontalScrollBarEnabled; }
bool UIScrollView::IsHorizontalScrollBarVisible() const { return m_HorizontalScrollBarVisible; }
void UIScrollView::SetScrollBarThickness(float value) { m_ScrollBarThickness = std::max(1.0f, value); SyncScrollBars(); }
float UIScrollView::GetScrollBarThickness() const { return m_ScrollBarThickness; }
void UIScrollView::SetMinimumThumbLength(float value) { m_MinimumThumbLength = std::max(1.0f, value); SyncScrollBars(); }
float UIScrollView::GetMinimumThumbLength() const { return m_MinimumThumbLength; }
void UIScrollView::SetPageScrollFactor(float value) { m_PageScrollFactor = std::max(0.0f, value); }
float UIScrollView::GetPageScrollFactor() const { return m_PageScrollFactor; }
void UIScrollView::SetScrollBarTrackColor(const math::Vec4& value) { m_ScrollBarTrackColor = value; SyncScrollBars(); }
const math::Vec4& UIScrollView::GetScrollBarTrackColor() const { return m_ScrollBarTrackColor; }
void UIScrollView::SetScrollBarThumbColor(const math::Vec4& value) { m_ScrollBarThumbColor = value; SyncScrollBars(); }
const math::Vec4& UIScrollView::GetScrollBarThumbColor() const { return m_ScrollBarThumbColor; }

void UIScrollView::OnMouseEvent(UIMouseEvent& event)
{
    if (m_DragAxis != ScrollBarDragAxis::None && HandleThumbDrag(event) == true) { return; }
    if (event.Type == UIMouseEventType::Down && event.Button == UIMouseButton::Left && HandleScrollBarMouseDown(event) == true) { return; }
    if (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left && (event.Target == m_VerticalScrollBar || event.Target == m_HorizontalScrollBar))
    {
        event.Handled = true;
        return;
    }
    if (event.Type != UIMouseEventType::Scroll) { return; }

    const math::Vec2 maxOffset = GetMaxScrollOffset();
    const float horizontalMagnitude = std::abs(event.ScrollDelta.x);
    const float verticalMagnitude = std::abs(event.ScrollDelta.y);
    math::Vec2 requested = m_ScrollOffset;

    // Trackpadの斜め入力で両軸が同時に動くのを避けるため、入力の大きい軸を1つだけ選択します。
    // 一般的なMouse WheelはYだけを送るため、VerticalにOverflowが無い場合だけHorizontalへFallbackします。
    if (verticalMagnitude >= horizontalMagnitude && verticalMagnitude > 0.0f)
    {
        if (maxOffset.y > 0.0f)
        {
            requested.y -= event.ScrollDelta.y * m_WheelScrollStep;
        }
        else if (maxOffset.x > 0.0f)
        {
            requested.x -= event.ScrollDelta.y * m_WheelScrollStep;
        }
    }
    else if (horizontalMagnitude > 0.0f && maxOffset.x > 0.0f)
    {
        requested.x += event.ScrollDelta.x * m_WheelScrollStep;
    }

    const math::Vec2 previous = m_ScrollOffset;
    SetScrollOffset(requested);
    if (m_ScrollOffset != previous) { event.Handled = true; }
}

void UIScrollView::OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const
{
    static_cast<void>(drawList);
    static_cast<void>(absolutePosition);
    const_cast<UIScrollView*>(this)->SyncScrollBars();
}

void UIScrollView::CreateScrollBarElements()
{
    auto vertical = CreateScope<UIScrollBarVisual>(true);
    // ScrollbarはViewport上のOverlayであり、表示状態やThicknessがScrollViewのDesiredSizeを変えてはいけません。
    vertical->SetAffectsParentMeasure(false);
    vertical->SetVisible(false);
    vertical->SetColors(m_ScrollBarTrackColor, m_ScrollBarThumbColor);
    m_VerticalScrollBar = static_cast<UIScrollBarVisual*>(AddChild(std::move(vertical)));

    auto horizontal = CreateScope<UIScrollBarVisual>(false);
    horizontal->SetAffectsParentMeasure(false);
    horizontal->SetVisible(false);
    horizontal->SetColors(m_ScrollBarTrackColor, m_ScrollBarThumbColor);
    m_HorizontalScrollBar = static_cast<UIScrollBarVisual*>(AddChild(std::move(horizontal)));
}

void UIScrollView::SyncScrollBars()
{
    if (m_VerticalScrollBar == nullptr || m_HorizontalScrollBar == nullptr) { return; }

    const math::Vec2 viewportSize = ResolveViewportSize();
    const math::Vec2 contentSize = ResolveContentSize();
    const math::Vec2 maxOffset(std::max(0.0f, contentSize.x - viewportSize.x), std::max(0.0f, contentSize.y - viewportSize.y));
    const math::Vec2 clampedOffset(std::clamp(m_ScrollOffset.x, 0.0f, maxOffset.x), std::clamp(m_ScrollOffset.y, 0.0f, maxOffset.y));
    if (clampedOffset != m_ScrollOffset)
    {
        m_ScrollOffset = clampedOffset;
        ApplyContentPosition();
    }

    m_VerticalScrollBarVisible = m_VerticalScrollBarEnabled == true && maxOffset.y > 0.0f && viewportSize.y > 0.0f;
    m_HorizontalScrollBarVisible = m_HorizontalScrollBarEnabled == true && maxOffset.x > 0.0f && viewportSize.x > 0.0f;
    if (m_VerticalScrollBar->IsVisible() != m_VerticalScrollBarVisible) { m_VerticalScrollBar->SetVisible(m_VerticalScrollBarVisible); }
    if (m_HorizontalScrollBar->IsVisible() != m_HorizontalScrollBarVisible) { m_HorizontalScrollBar->SetVisible(m_HorizontalScrollBarVisible); }
    m_VerticalScrollBar->SetColors(m_ScrollBarTrackColor, m_ScrollBarThumbColor);
    m_HorizontalScrollBar->SetColors(m_ScrollBarTrackColor, m_ScrollBarThumbColor);

    if (m_VerticalScrollBarVisible == true)
    {
        const float trackLength = std::max(0.0f, viewportSize.y - (m_HorizontalScrollBarVisible == true ? m_ScrollBarThickness : 0.0f));
        const math::Vec2 position(std::max(0.0f, viewportSize.x - m_ScrollBarThickness), 0.0f);
        const math::Vec2 size(m_ScrollBarThickness, trackLength);
        if (m_VerticalScrollBar->GetPosition() != position) { m_VerticalScrollBar->SetPosition(position); }
        if (m_VerticalScrollBar->GetSize() != size) { m_VerticalScrollBar->SetSize(size); }
        const float ratio = contentSize.y > 0.0f ? std::clamp(viewportSize.y / contentSize.y, 0.0f, 1.0f) : 1.0f;
        const float thumbLength = std::min(trackLength, std::max(m_MinimumThumbLength, trackLength * ratio));
        const float travel = std::max(0.0f, trackLength - thumbLength);
        const float thumbStart = maxOffset.y > 0.0f ? travel * (m_ScrollOffset.y / maxOffset.y) : 0.0f;
        m_VerticalScrollBar->SetThumbGeometry(thumbStart, thumbLength);
    }

    if (m_HorizontalScrollBarVisible == true)
    {
        const float trackLength = std::max(0.0f, viewportSize.x - (m_VerticalScrollBarVisible == true ? m_ScrollBarThickness : 0.0f));
        const math::Vec2 position(0.0f, std::max(0.0f, viewportSize.y - m_ScrollBarThickness));
        const math::Vec2 size(trackLength, m_ScrollBarThickness);
        if (m_HorizontalScrollBar->GetPosition() != position) { m_HorizontalScrollBar->SetPosition(position); }
        if (m_HorizontalScrollBar->GetSize() != size) { m_HorizontalScrollBar->SetSize(size); }
        const float ratio = contentSize.x > 0.0f ? std::clamp(viewportSize.x / contentSize.x, 0.0f, 1.0f) : 1.0f;
        const float thumbLength = std::min(trackLength, std::max(m_MinimumThumbLength, trackLength * ratio));
        const float travel = std::max(0.0f, trackLength - thumbLength);
        const float thumbStart = maxOffset.x > 0.0f ? travel * (m_ScrollOffset.x / maxOffset.x) : 0.0f;
        m_HorizontalScrollBar->SetThumbGeometry(thumbStart, thumbLength);
    }
}

void UIScrollView::ApplyContentPosition()
{
    if (m_Content == nullptr) { return; }
    const math::Vec2 position(-m_ScrollOffset.x, -m_ScrollOffset.y);
    if (m_Content->GetPosition() != position) { m_Content->SetPosition(position); }
}

void UIScrollView::EndThumbDrag()
{
    // Mouse Captureの所有者はUIScrollViewなので、Pressed Visualだけは内部Scrollbarへ明示的に同期します。
    // 解除経路をここへ集約することでMouseUp / Cancel / Content差し替え / Axis無効化の全てで状態を残しません。
    if (m_VerticalScrollBar != nullptr)
    {
        m_VerticalScrollBar->SetThumbPressed(false);
    }
    if (m_HorizontalScrollBar != nullptr)
    {
        m_HorizontalScrollBar->SetThumbPressed(false);
    }

    m_DragAxis = ScrollBarDragAxis::None;
    m_DragGrabOffset = 0.0f;
}

bool UIScrollView::HandleScrollBarMouseDown(UIMouseEvent& event)
{
    const bool vertical = event.Target == m_VerticalScrollBar;
    const bool horizontal = event.Target == m_HorizontalScrollBar;
    if (vertical == false && horizontal == false) { return false; }

    UIScrollBarVisual* scrollBar = vertical == true ? m_VerticalScrollBar : m_HorizontalScrollBar;
    if (scrollBar == nullptr || scrollBar->IsVisible() == false) { return false; }
    math::Vec2 localPosition;
    if (scrollBar->TryScreenToLocalPosition(event.ScreenPosition, localPosition) == false) { return false; }

    const float pointer = vertical == true ? localPosition.y : localPosition.x;
    const float thumbStart = scrollBar->GetThumbStart();
    const float thumbEnd = thumbStart + scrollBar->GetThumbLength();
    if (pointer >= thumbStart && pointer < thumbEnd)
    {
        if (event.Context != nullptr && event.Context->CaptureMouse(this) == true)
        {
            m_DragAxis = vertical == true ? ScrollBarDragAxis::Vertical : ScrollBarDragAxis::Horizontal;
            m_DragGrabOffset = pointer - thumbStart;
            scrollBar->SetThumbPressed(true);
        }
    }
    else
    {
        PageScroll(vertical, pointer >= thumbEnd);
    }
    event.Handled = true;
    return true;
}

bool UIScrollView::HandleThumbDrag(UIMouseEvent& event)
{
    if (event.Type == UIMouseEventType::Cancel)
    {
        EndThumbDrag();
        event.Handled = true;
        return true;
    }
    if (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left)
    {
        if (event.Context != nullptr) { event.Context->ReleaseMouseCapture(this); }
        EndThumbDrag();
        event.Handled = true;
        return true;
    }
    if (event.Type != UIMouseEventType::Move) { return false; }

    const bool vertical = m_DragAxis == ScrollBarDragAxis::Vertical;
    UIScrollBarVisual* scrollBar = vertical == true ? m_VerticalScrollBar : m_HorizontalScrollBar;
    if (scrollBar == nullptr || scrollBar->IsVisible() == false)
    {
        if (event.Context != nullptr) { event.Context->ReleaseMouseCapture(this); }
        EndThumbDrag();
        event.Handled = true;
        return true;
    }

    math::Vec2 localPosition;
    if (scrollBar->TryScreenToLocalPosition(event.ScreenPosition, localPosition) == false) { return false; }
    const float pointer = vertical == true ? localPosition.y : localPosition.x;
    const float trackLength = vertical == true ? scrollBar->GetSize().y : scrollBar->GetSize().x;
    const float thumbLength = scrollBar->GetThumbLength();
    const float travel = std::max(0.0f, trackLength - thumbLength);
    const math::Vec2 maxOffset = GetMaxScrollOffset();
    const float maxAxisOffset = vertical == true ? maxOffset.y : maxOffset.x;
    if (travel <= 0.0f || maxAxisOffset <= 0.0f)
    {
        event.Handled = true;
        return true;
    }

    const float thumbStart = std::clamp(pointer - m_DragGrabOffset, 0.0f, travel);
    const float axisOffset = maxAxisOffset * (thumbStart / travel);
    math::Vec2 requested = m_ScrollOffset;
    if (vertical == true) { requested.y = axisOffset; } else { requested.x = axisOffset; }
    SetScrollOffset(requested);
    event.Handled = true;
    return true;
}

void UIScrollView::PageScroll(bool vertical, bool forward)
{
    const math::Vec2 viewportSize = ResolveViewportSize();
    math::Vec2 requested = m_ScrollOffset;
    const float direction = forward == true ? 1.0f : -1.0f;
    if (vertical == true) { requested.y += direction * viewportSize.y * m_PageScrollFactor; }
    else { requested.x += direction * viewportSize.x * m_PageScrollFactor; }
    SetScrollOffset(requested);
}

math::Vec2 UIScrollView::ResolveViewportSize() const
{
    const math::Vec2& arranged = GetSize();
    const math::Vec2& preferred = GetPreferredSize();
    return math::Vec2(arranged.x > 0.0f ? arranged.x : preferred.x, arranged.y > 0.0f ? arranged.y : preferred.y);
}

math::Vec2 UIScrollView::ResolveContentSize() const
{
    if (m_Content == nullptr) { return math::Vec2(0.0f, 0.0f); }
    const math::Vec2& arranged = m_Content->GetSize();
    const math::Vec2& desired = m_Content->GetDesiredSize();
    const math::Vec2& preferred = m_Content->GetPreferredSize();
    return math::Vec2(std::max(arranged.x, std::max(desired.x, preferred.x)), std::max(arranged.y, std::max(desired.y, preferred.y)));
}

} // namespace Raven
