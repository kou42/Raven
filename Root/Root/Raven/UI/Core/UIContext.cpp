#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Core/UIHitTest.h"

#include <utility>

namespace Raven
{

UIContext::UIContext()
    : m_RootElement(CreateScope<UIElement>())
{
}

void UIContext::BeginFrame(const math::Vec2& viewportSize)
{
    // 前frameのDrawCommandを必ず破棄してから新しいframeを開始します。
    // UI TreeはRetained Modeとして保持しますが、DrawListはViewport/Layout結果から
    // 毎frame再構築することでResizeやStyle変更を即座に反映できるようにします。
    m_DrawList.Clear();
    m_ViewportSize = viewportSize;
    m_FrameActive = true;
}

void UIContext::EndFrame()
{
    if (m_FrameActive == false)
    {
        return;
    }

    // ====================================================================
    // Retained UI Tree -> Layout -> UIDrawList
    // ====================================================================
    // UIElement Treeはframeを跨いで保持し、描画直前にAbsolute / Vertical / Horizontal Layoutを解決して
    // 今frame用DrawCommandへ展開します。将来Measure / Arrangeを分離してもUIContextのframe境界は維持します。
    if (m_RootElement != nullptr)
    {
        m_RootElement->BuildDrawList(m_DrawList);
    }

    // Renderer backendがまだ設定されていない期間でもUI構築側を先行実装できるよう、
    // nullptrは正常な状態として扱います。OpenGLUIRenderer追加後はApplication初期化時に
    // SetRenderer()して、この同じframe境界から実描画へ接続します。
    if (m_Renderer != nullptr)
    {
        m_Renderer->Render(m_DrawList, m_ViewportSize);
    }

    m_FrameActive = false;
}

bool UIContext::RouteMouseEvent(
    UIMouseEventType type,
    const math::Vec2& screenPosition,
    UIMouseButton button)
{
    if (m_RootElement == nullptr)
    {
        return false;
    }

    // Hit TestはArrange済みのPosition / Sizeを参照します。
    // 入力がEndFrame()より先に届いても古いLayoutを使わないよう、Dirty時はここでLayoutを解決します。
    // 現在のUIElementはLayout専用APIをまだ公開していないため、BuildDrawList()を一時Listに対して呼びます。
    // Layout APIをMeasure / Arrangeとして公開した段階で、この一時DrawListは不要になります。
    if (m_RootElement->IsMeasureDirty() == true || m_RootElement->IsArrangeDirty() == true)
    {
        UIDrawList layoutResolveDrawList;
        m_RootElement->BuildDrawList(layoutResolveDrawList);
    }

    // Hoverは常に実際のPointer位置を表す必要があるため、Capture中でもHit Test結果から更新します。
    UIElement* hitTarget = UIHitTest::FindTopmost(*m_RootElement, screenPosition);
    UpdateHoverTarget(hitTarget);

    // Pressedは「どのElement上で押し始めたか」を保持する状態です。
    // MouseUpでは解除前のElementをEventへ保存し、ButtonがDown開始ElementとUp時Targetを比較できるようにします。
    UIElement* pressedTargetForEvent = m_PressedElement;
    if (type == UIMouseEventType::Down && button == UIMouseButton::Left)
    {
        UpdatePressedTarget(hitTarget);
        pressedTargetForEvent = m_PressedElement;
    }

    // Capture Elementが存在する間はEvent配送先を固定します。
    // Hit TargetとCapture Targetを分離することで、Hover表示は実位置を維持しながらDrag操作だけ継続できます。
    UIElement* routeTarget = m_MouseCaptureElement;
    if (routeTarget == nullptr)
    {
        routeTarget = hitTarget;
    }

    UIMouseEvent event;
    event.Type = type;
    event.Button = button;
    event.ScreenPosition = screenPosition;
    event.Context = this;
    event.Target = routeTarget;
    event.PressedTarget = pressedTargetForEvent;

    bool handled = false;
    if (routeTarget != nullptr)
    {
        // Target -> Parent -> ... -> Root のBubble方式です。
        // WidgetがHandledを立てた時点で親への伝播を止めます。
        // Capture中はCapture ElementをTargetとして同じBubble規則を適用します。
        UIElement* current = routeTarget;
        while (current != nullptr)
        {
            event.CurrentTarget = current;
            current->HandleMouseEvent(event);

            if (event.Handled == true)
            {
                break;
            }

            current = current->GetParent();
        }
        handled = event.Handled;
    }

    // MouseUpのHandlerはPressedTargetを参照するため、Routing完了後に状態を解除します。
    // Hit先がnullptrでも必ず解除し、UI外で離した場合のPressed残留を防ぎます。
    if (type == UIMouseEventType::Up && button == UIMouseButton::Left)
    {
        UpdatePressedTarget(nullptr);
    }

    return handled;
}

bool UIContext::RouteMouseMove(const math::Vec2& screenPosition)
{
    return RouteMouseEvent(UIMouseEventType::Move, screenPosition, UIMouseButton::None);
}

bool UIContext::RouteMouseDown(const math::Vec2& screenPosition, UIMouseButton button)
{
    return RouteMouseEvent(UIMouseEventType::Down, screenPosition, button);
}

bool UIContext::RouteMouseUp(const math::Vec2& screenPosition, UIMouseButton button)
{
    return RouteMouseEvent(UIMouseEventType::Up, screenPosition, button);
}

bool UIContext::CaptureMouse(UIElement* element)
{
    if (element == nullptr)
    {
        return false;
    }

    if (m_MouseCaptureElement != nullptr && m_MouseCaptureElement != element)
    {
        return false;
    }

    m_MouseCaptureElement = element;
    return true;
}

void UIContext::ReleaseMouseCapture(UIElement* element)
{
    // Capture所有者だけが解除できます。
    // 他Widgetが誤って現在のDrag操作を終了させることを防ぎます。
    if (element == nullptr || m_MouseCaptureElement != element)
    {
        return;
    }

    m_MouseCaptureElement = nullptr;
}

void UIContext::ReleaseMouseCapture()
{
    m_MouseCaptureElement = nullptr;
}

bool UIContext::HasMouseCapture() const
{
    return m_MouseCaptureElement != nullptr;
}

bool UIContext::HasMouseCapture(const UIElement* element) const
{
    return element != nullptr && m_MouseCaptureElement == element;
}

UIElement* UIContext::GetMouseCaptureElement() { return m_MouseCaptureElement; }
const UIElement* UIContext::GetMouseCaptureElement() const { return m_MouseCaptureElement; }

void UIContext::SetRenderer(Scope<UIRenderer> renderer)
{
    m_Renderer = std::move(renderer);
}

UIElement& UIContext::GetRootElement() { return *m_RootElement; }
const UIElement& UIContext::GetRootElement() const { return *m_RootElement; }

UIElement* UIContext::GetHoveredElement() { return m_HoveredElement; }
const UIElement* UIContext::GetHoveredElement() const { return m_HoveredElement; }
UIElement* UIContext::GetPressedElement() { return m_PressedElement; }
const UIElement* UIContext::GetPressedElement() const { return m_PressedElement; }

UIDrawList& UIContext::GetDrawList() { return m_DrawList; }
const UIDrawList& UIContext::GetDrawList() const { return m_DrawList; }

const math::Vec2& UIContext::GetViewportSize() const { return m_ViewportSize; }
bool UIContext::IsFrameActive() const { return m_FrameActive; }

void UIContext::UpdateHoverTarget(UIElement* target)
{
    if (m_HoveredElement == target)
    {
        return;
    }

    if (m_HoveredElement != nullptr)
    {
        m_HoveredElement->SetHovered(false);
    }

    m_HoveredElement = target;
    if (m_HoveredElement != nullptr)
    {
        m_HoveredElement->SetHovered(true);
    }
}

void UIContext::UpdatePressedTarget(UIElement* target)
{
    if (m_PressedElement == target)
    {
        return;
    }

    if (m_PressedElement != nullptr)
    {
        m_PressedElement->SetPressed(false);
    }

    m_PressedElement = target;
    if (m_PressedElement != nullptr)
    {
        m_PressedElement->SetPressed(true);
    }
}

} // namespace Raven
