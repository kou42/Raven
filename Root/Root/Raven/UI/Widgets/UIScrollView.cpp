#include "Raven/UI/Widgets/UIScrollView.h"

#include <algorithm>

namespace Raven
{

UIScrollView::UIScrollView()
{
    // ScrollViewのViewport外へ出たContent描画とHit Testを共通Clipで除外します。
    SetClipChildren(true);
    SetLayoutMode(UILayoutMode::Absolute);
}

UIElement* UIScrollView::SetContent(Scope<UIElement> content)
{
    ClearChildren();
    m_ScrollOffset = math::Vec2(0.0f, 0.0f);

    if (content == nullptr)
    {
        return nullptr;
    }

    content->SetPosition(math::Vec2(0.0f, 0.0f));
    return AddChild(std::move(content));
}

UIElement* UIScrollView::GetContent()
{
    const auto& children = GetChildren();
    if (children.empty() || children.front() == nullptr)
    {
        return nullptr;
    }
    return children.front().get();
}

const UIElement* UIScrollView::GetContent() const
{
    const auto& children = GetChildren();
    if (children.empty() || children.front() == nullptr)
    {
        return nullptr;
    }
    return children.front().get();
}

void UIScrollView::SetScrollOffset(const math::Vec2& value)
{
    const math::Vec2 maxOffset = GetMaxScrollOffset();
    m_ScrollOffset.x = std::clamp(value.x, 0.0f, maxOffset.x);
    m_ScrollOffset.y = std::clamp(value.y, 0.0f, maxOffset.y);
    ApplyContentPosition();
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
    SetScrollOffset(m_ScrollOffset);
}

void UIScrollView::SetWheelScrollStep(float value)
{
    m_WheelScrollStep = std::max(0.0f, value);
}

float UIScrollView::GetWheelScrollStep() const
{
    return m_WheelScrollStep;
}

void UIScrollView::OnMouseEvent(UIMouseEvent& event)
{
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

math::Vec2 UIScrollView::ResolveViewportSize() const
{
    const math::Vec2& arranged = GetSize();
    const math::Vec2& preferred = GetPreferredSize();
    return math::Vec2(
        std::max(arranged.x, preferred.x),
        std::max(arranged.y, preferred.y));
}

math::Vec2 UIScrollView::ResolveContentSize() const
{
    const UIElement* content = GetContent();
    if (content == nullptr)
    {
        return math::Vec2(0.0f, 0.0f);
    }

    // Programmatic Scrollは初回Arrange前にも呼ばれ得るため、Arranged / Desired / Preferredの
    // うち現在判明している最大値をContent Sizeとして利用します。
    const math::Vec2& arranged = content->GetSize();
    const math::Vec2& desired = content->GetDesiredSize();
    const math::Vec2& preferred = content->GetPreferredSize();
    return math::Vec2(
        std::max(arranged.x, std::max(desired.x, preferred.x)),
        std::max(arranged.y, std::max(desired.y, preferred.y)));
}

void UIScrollView::ApplyContentPosition()
{
    UIElement* content = GetContent();
    if (content == nullptr)
    {
        return;
    }

    // Offsetが増えるほどContentを左上方向へ移動し、Viewport内で後方部分を表示します。
    content->SetPosition(math::Vec2(-m_ScrollOffset.x, -m_ScrollOffset.y));
}

} // namespace Raven
