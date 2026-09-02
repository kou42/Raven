#include "Raven/UI/Widgets/UIScrollView.h"
#include "Raven/UI/Core/UIContext.h"

#include <algorithm>

namespace Raven
{

// ============================================================================
// UIScrollBarVisual
// ============================================================================
// ScrollbarはScrollView内部の実UIElementとして保持します。
// Track / Thumb描画だけを担当し、Scroll Offset更新やMouse CaptureはOwnerであるUIScrollViewへ集約します。
// こうすることでScrollbar自体がHit Test対象になりつつ、Scroll状態を複数Elementへ分散させません。
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
    }

protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override
    {
        const math::Vec2& size = GetSize();
        drawList.AddRect(
            absolutePosition,
            math::Vec2(absolutePosition.x + size.x, absolutePosition.y + size.y),
            ApplyVisualColor(m_TrackColor));

        if (m_ThumbLength <= 0.0f)
        {
            return;
        }

        math::Vec2 thumbMin = absolutePosition;
        math::Vec2 thumbMax(
            absolutePosition.x + size.x,
            absolutePosition.y + size.y);

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

        drawList.AddRect(
            thumbMin,
            thumbMax,
            ApplyVisualColor(m_ThumbColor));
    }

private:
    bool m_Vertical = false;
    float m_ThumbStart = 0.0f;
    float m_ThumbLength = 0.0f;
    math::Vec4 m_TrackColor{ 0.06f, 0.07f, 0.09f, 0.75f };
    math::Vec4 m_ThumbColor{ 0.42f, 0.45f, 0.52f, 0.95f };
};

UIScrollView::UIScrollView()
{
    // ScrollViewのViewport外へ出たContent描画とHit Testを共通Clipで除外します。
    SetClipChildren(true);
    SetLayoutMode(UILayoutMode::Absolute);
    CreateScrollBarElements();
}

UIElement* UIScrollView::SetContent(Scope<UIElement> content)
{
    // ScrollbarをContentより後へ登録しPainter's Order / Hit Testの両方で前面に置くため、
    // Content置換時は内部Elementも一度再構築します。
    ClearChildren();
    m_Content = nullptr;
    m_VerticalScrollBar = nullptr;
    m_HorizontalScrollBar = nullptr;
    m_ScrollOffset = math::Vec2(0.0f, 0.0f);
    m_DragAxis = ScrollBarDragAxis::None;
    m_DragGrabOffset = 0.0f;

    if (content != nullptr)
    {
        content->SetPosition(math::Vec2(0.0f, 0.0f));
        m_Content = AddChild(std::move(content));
    }

    CreateScrollBarElements();
    SyncScrollBars();
    return m_Content;
}

UIElement* UIScrollView::GetContent()
{
    return m_Content;
}

const UIElement* UIScrollView::GetContent() const
{
    return m_Content;
}

void UIScrollView::SetScrollOffset(const math::Vec2& value)
{
    const math::Vec2 maxOffset = GetMaxScrollOffset();
    const math::Vec2 clamped(
        std::clamp(value.x, 0.0f, maxOffset.x),
        std::clamp(value.y, 0.0f, maxOffset.y));

    if (clamped != m_ScrollOffset)
    {
        m_ScrollOffset = clamped;
        ApplyContentPosition();
    }

    SyncScrollBars();
}

const math::Vec2& UIScrollView::GetScrollOffset() const
{
    return m_ScrollOffset;
}

math::Vec2 UIScrollView::GetMaxScrollOffset() const
{
    const math::Vec2 viewportSize = ResolveViewportSize();
    const math::Vec2 contentSize = ResolveContentSize();
    return math::Vec2(
        std::max(0.0f, contentSize.x - viewportSize.x),
        std::max(0.0f, contentSize.y - viewportSize.y));
}

void UIScrollView::RefreshScrollRange()
{
    SyncScrollBars();
}

void UIScrollView::SetWheelScrollStep(float value)
{
    m_WheelScrollStep = std::max(0.0f, value);
}

float UIScrollView::GetWheelScrollStep() const
{
    return m_WheelScrollStep;
}

void UIScrollView::SetVerticalScrollBarEnabled(bool value)
{
    m_VerticalScrollBarEnabled = value;
    SyncScrollBars();
}

bool UIScrollView::IsVerticalScrollBarEnabled() const
{
    return m_VerticalScrollBarEnabled;
}

bool UIScrollView::IsVerticalScrollBarVisible() const
{
    return m_VerticalScrollBarVisible;
}

void UIScrollView::SetHorizontalScrollBarEnabled(bool value)
{
    m_HorizontalScrollBarEnabled = value;
    SyncScrollBars();
}

bool UIScrollView::IsHorizontalScrollBarEnabled() const
{
    return m_HorizontalScrollBarEnabled;
}

bool UIScrollView::IsHorizontalScrollBarVisible() const
{
    return m_HorizontalScrollBarVisible;
}

void UIScrollView::SetScrollBarThickness(float value)
{
    m_ScrollBarThickness = std::max(1.0f, value);
    SyncScrollBars();
}

float UIScrollView::GetScrollBarThickness() const
{
    return m_ScrollBarThickness;
}

void UIScrollView::SetMinimumThumbLength(float value)
{
    m_MinimumThumbLength = std::max(1.0f, value);
    SyncScrollBars();
}

float UIScrollView::GetMinimumThumbLength() const
{
    return m_MinimumThumbLength;
}

void UIScrollView::SetPageScrollFactor(float value)
{
    m_PageScrollFactor = std::max(0.0f, value);
}

float UIScrollView::GetPageScrollFactor() const
{
    return m_PageScrollFactor;
}

void UIScrollView::SetScrollBarTrackColor(const math::Vec4& value)
{
    m_ScrollBarTrackColor = value;
    SyncScrollBars();
}

const math::Vec4& UIScrollView::GetScrollBarTrackColor() const
{
    return m_ScrollBarTrackColor;
}

void UIScrollView::SetScrollBarThumbColor(const math::Vec4& value)
{
    m_ScrollBarThumbColor = value;
    SyncScrollBars();
}

const math::Vec4& UIScrollView::GetScrollBarThumbColor() const
{
    return m_ScrollBarThumbColor;
}

void UIScrollView::OnMouseEvent(UIMouseEvent& event)
{
    // Thumb Drag中はCapture TargetがUIScrollView自身になるため、通常のTarget判定より先に処理します。
    if (m_DragAxis != ScrollBarDragAxis::None)
    {
        if (HandleThumbDrag(event) == true)
        {
            return;
        }
    }

    if (event.Type == UIMouseEventType::Down && event.Button == UIMouseButton::Left)
    {
        if (HandleScrollBarMouseDown(event) == true)
        {
            return;
        }
    }

    // Track ClickではCaptureしないため、対応するMouse UpもScrollbarで消費して背後Layerへの入力漏れを防ぎます。
    if (event.Type == UIMouseEventType::Up &&
        event.Button == UIMouseButton::Left &&
        (event.Target == m_VerticalScrollBar || event.Target == m_HorizontalScrollBar))
    {
        event.Handled = true;
        return;
    }

    if (event.Type != UIMouseEventType::Scroll)
    {
        return;
    }

    const math::Vec2 previous = m_ScrollOffset;

    // GLFWでは正のX Offsetは右方向、正のY Offsetは上方向のScrollを表します。
    // Content Offsetは「Viewport左上からContentをどれだけ進めたか」で保持するため、
    // 横方向は正Offsetを加算し、縦方向は上ScrollでOffsetを戻すよう符号を分けます。
    const math::Vec2 requested(
        m_ScrollOffset.x + event.ScrollDelta.x * m_WheelScrollStep,
        m_ScrollOffset.y - event.ScrollDelta.y * m_WheelScrollStep);
    SetScrollOffset(requested);

    // 実際にOffsetが変わった場合だけ消費します。
    // 端まで到達した内側ScrollViewではHandledを立てないため、Bubbleで外側ScrollViewへScrollを渡せます。
    if (m_ScrollOffset != previous)
    {
        event.Handled = true;
    }
}

void UIScrollView::OnBuildDrawList(
    UIDrawList& drawList,
    const math::Vec2& absolutePosition) const
{
    static_cast<void>(drawList);
    static_cast<void>(absolutePosition);

    // Layout後の確定Sizeを使ってScrollbar Geometryを更新します。
    // UIElementの描画順はSelf -> Childrenなので、この時点で更新すれば直後に描かれるScrollbarへ同frameで反映できます。
    // Geometryが変化した場合だけUIElement Setterを呼ぶため、安定frameで不要なDirty伝播は発生しません。
    const_cast<UIScrollView*>(this)->SyncScrollBars();
}

void UIScrollView::CreateScrollBarElements()
{
    auto vertical = CreateScope<UIScrollBarVisual>(true);
    vertical->SetVisible(false);
    vertical->SetColors(m_ScrollBarTrackColor, m_ScrollBarThumbColor);
    m_VerticalScrollBar = static_cast<UIScrollBarVisual*>(AddChild(std::move(vertical)));

    auto horizontal = CreateScope<UIScrollBarVisual>(false);
    horizontal->SetVisible(false);
    horizontal->SetColors(m_ScrollBarTrackColor, m_ScrollBarThumbColor);
    m_HorizontalScrollBar = static_cast<UIScrollBarVisual*>(AddChild(std::move(horizontal)));
}

void UIScrollView::SyncScrollBars()
{
    if (m_VerticalScrollBar == nullptr || m_HorizontalScrollBar == nullptr)
    {
        return;
    }

    const math::Vec2 viewportSize = ResolveViewportSize();
    const math::Vec2 contentSize = ResolveContentSize();
    const math::Vec2 maxOffset(
        std::max(0.0f, contentSize.x - viewportSize.x),
        std::max(0.0f, contentSize.y - viewportSize.y));

    const math::Vec2 clampedOffset(
        std::clamp(m_ScrollOffset.x, 0.0f, maxOffset.x),
        std::clamp(m_ScrollOffset.y, 0.0f, maxOffset.y));
    if (clampedOffset != m_ScrollOffset)
    {
        m_ScrollOffset = clampedOffset;
        ApplyContentPosition();
    }

    m_VerticalScrollBarVisible =
        m_VerticalScrollBarEnabled == true &&
        maxOffset.y > 0.0f &&
        viewportSize.y > 0.0f;
    m_HorizontalScrollBarVisible =
        m_HorizontalScrollBarEnabled == true &&
        maxOffset.x > 0.0f &&
        viewportSize.x > 0.0f;

    m_VerticalScrollBar->SetVisible(m_VerticalScrollBarVisible);
    m_HorizontalScrollBar->SetVisible(m_HorizontalScrollBarVisible);
    m_VerticalScrollBar->SetColors(m_ScrollBarTrackColor, m_ScrollBarThumbColor);
    m_HorizontalScrollBar->SetColors(m_ScrollBarTrackColor, m_ScrollBarThumbColor);

    if (m_VerticalScrollBarVisible == true)
    {
        const float trackLength = std::max(
            0.0f,
            viewportSize.y - (m_HorizontalScrollBarVisible == true ? m_ScrollBarThickness : 0.0f));
        const math::Vec2 position(
            std::max(0.0f, viewportSize.x - m_ScrollBarThickness),
            0.0f);
        const math::Vec2 size(m_ScrollBarThickness, trackLength);

        if (m_VerticalScrollBar->GetPosition() != position)
        {
            m_VerticalScrollBar->SetPosition(position);
        }
        if (m_VerticalScrollBar->GetSize() != size)
        {
            m_VerticalScrollBar->SetSize(size);
        }

        const float ratio = contentSize.y > 0.0f
            ? std::clamp(viewportSize.y / contentSize.y, 0.0f, 1.0f)
            : 1.0f;
        const float thumbLength = std::min(
            trackLength,
            std::max(m_MinimumThumbLength, trackLength * ratio));
        const float travel = std::max(0.0f, trackLength - thumbLength);
        const float thumbStart = maxOffset.y > 0.0f
            ? travel * (m_ScrollOffset.y / maxOffset.y)
            : 0.0f;
        m_VerticalScrollBar->SetThumbGeometry(thumbStart, thumbLength);
    }

    if (m_HorizontalScrollBarVisible == true)
    {
        const float trackLength = std::max(
            0.0f,
            viewportSize.x - (m_VerticalScrollBarVisible == true ? m_ScrollBarThickness : 0.0f));
        const math::Vec2 position(
            0.0f,
            std::max(0.0f, viewportSize.y - m_ScrollBarThickness));
        const math::Vec2 size(trackLength, m_ScrollBarThickness);

        if (m_HorizontalScrollBar->GetPosition() != position)
        {
            m_HorizontalScrollBar->SetPosition(position);
        }
        if (m_HorizontalScrollBar->GetSize() != size)
        {
            m_HorizontalScrollBar->SetSize(size);
        }

        const float ratio = contentSize.x > 0.0f
            ? std::clamp(viewportSize.x / contentSize.x, 0.0f, 1.0f)
            : 1.0f;
        const float thumbLength = std::min(
            trackLength,
            std::max(m_MinimumThumbLength, trackLength * ratio));
        const float travel = std::max(0.0f, trackLength - thumbLength);
        const float thumbStart = maxOffset.x > 0.0f
            ? travel * (m_ScrollOffset.x / maxOffset.x)
            : 0.0f;
        m_HorizontalScrollBar->SetThumbGeometry(thumbStart, thumbLength);
    }
}

void UIScrollView::ApplyContentPosition()
{
    if (m_Content == nullptr)
    {
        return;
    }

    // Offsetが増えるほどContentを左上方向へ移動し、Viewport内で後方部分を表示します。
    const math::Vec2 position(-m_ScrollOffset.x, -m_ScrollOffset.y);
    if (m_Content->GetPosition() != position)
    {
        m_Content->SetPosition(position);
    }
}

void UIScrollView::EndThumbDrag()
{
    m_DragAxis = ScrollBarDragAxis::None;
    m_DragGrabOffset = 0.0f;
}

bool UIScrollView::HandleScrollBarMouseDown(UIMouseEvent& event)
{
    const bool vertical = event.Target == m_VerticalScrollBar;
    const bool horizontal = event.Target == m_HorizontalScrollBar;
    if (vertical == false && horizontal == false)
    {
        return false;
    }

    UIScrollBarVisual* scrollBar = vertical == true
        ? m_VerticalScrollBar
        : m_HorizontalScrollBar;
    if (scrollBar == nullptr || scrollBar->IsVisible() == false)
    {
        return false;
    }

    math::Vec2 localPosition;
    if (scrollBar->TryScreenToLocalPosition(event.ScreenPosition, localPosition) == false)
    {
        return false;
    }

    const float pointer = vertical == true ? localPosition.y : localPosition.x;
    const float thumbStart = scrollBar->GetThumbStart();
    const float thumbEnd = thumbStart + scrollBar->GetThumbLength();

    if (pointer >= thumbStart && pointer < thumbEnd)
    {
        // Thumb上で押した位置を保持し、Drag開始時にThumbがPointer中央へJumpしないようにします。
        if (event.Context != nullptr && event.Context->CaptureMouse(this) == true)
        {
            m_DragAxis = vertical == true
                ? ScrollBarDragAxis::Vertical
                : ScrollBarDragAxis::Horizontal;
            m_DragGrabOffset = pointer - thumbStart;
        }
    }
    else
    {
        // Track空白部ClickはThumb方向へViewport比のPage Scrollを行います。
        // Thumbより手前なら戻し、Thumbより後ろなら進めます。
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
        if (event.Context != nullptr)
        {
            event.Context->ReleaseMouseCapture(this);
        }
        EndThumbDrag();
        event.Handled = true;
        return true;
    }

    if (event.Type != UIMouseEventType::Move)
    {
        return false;
    }

    const bool vertical = m_DragAxis == ScrollBarDragAxis::Vertical;
    UIScrollBarVisual* scrollBar = vertical == true
        ? m_VerticalScrollBar
        : m_HorizontalScrollBar;
    if (scrollBar == nullptr || scrollBar->IsVisible() == false)
    {
        // Drag中にOverflow解消やScrollbar無効化が起きた場合も、次のPointer EventでCaptureを確実に解放します。
        if (event.Context != nullptr)
        {
            event.Context->ReleaseMouseCapture(this);
        }
        EndThumbDrag();
        event.Handled = true;
        return true;
    }

    math::Vec2 localPosition;
    if (scrollBar->TryScreenToLocalPosition(event.ScreenPosition, localPosition) == false)
    {
        return false;
    }

    const float pointer = vertical == true ? localPosition.y : localPosition.x;
    const float trackLength = vertical == true
        ? scrollBar->GetSize().y
        : scrollBar->GetSize().x;
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
    if (vertical == true)
    {
        requested.y = axisOffset;
    }
    else
    {
        requested.x = axisOffset;
    }
    SetScrollOffset(requested);

    event.Handled = true;
    return true;
}

void UIScrollView::PageScroll(bool vertical, bool forward)
{
    const math::Vec2 viewportSize = ResolveViewportSize();
    math::Vec2 requested = m_ScrollOffset;
    const float direction = forward == true ? 1.0f : -1.0f;

    if (vertical == true)
    {
        requested.y += direction * viewportSize.y * m_PageScrollFactor;
    }
    else
    {
        requested.x += direction * viewportSize.x * m_PageScrollFactor;
    }

    SetScrollOffset(requested);
}

math::Vec2 UIScrollView::ResolveViewportSize() const
{
    const math::Vec2& arranged = GetSize();
    const math::Vec2& preferred = GetPreferredSize();

    // Arrange後は実際のViewport Sizeを優先します。
    // 初回Arrange前だけPreferred SizeへFallbackし、親Layoutで縮小されたScrollViewを過大評価しないようにします。
    return math::Vec2(
        arranged.x > 0.0f ? arranged.x : preferred.x,
        arranged.y > 0.0f ? arranged.y : preferred.y);
}

math::Vec2 UIScrollView::ResolveContentSize() const
{
    if (m_Content == nullptr)
    {
        return math::Vec2(0.0f, 0.0f);
    }

    // Programmatic Scrollは初回Arrange前にも呼ばれ得るため、Arranged / Desired / Preferredの
    // うち現在判明している最大値をContent Sizeとして利用します。
    const math::Vec2& arranged = m_Content->GetSize();
    const math::Vec2& desired = m_Content->GetDesiredSize();
    const math::Vec2& preferred = m_Content->GetPreferredSize();
    return math::Vec2(
        std::max(arranged.x, std::max(desired.x, preferred.x)),
        std::max(arranged.y, std::max(desired.y, preferred.y)));
}

} // namespace Raven
