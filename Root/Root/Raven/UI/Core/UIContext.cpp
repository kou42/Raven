#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Core/UIHitTest.h"

#include <utility>

namespace Raven
{

UIContext::UIContext()
    : m_RootElement(CreateScope<UIElement>())
{
    // Rootから追加される全ElementへContext所属を伝播し、Tree変更時にInteraction Stateを安全に掃除できるようにします。
    m_RootElement->SetContextRecursive(this);
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
        BeginMouseDispatch();

        // Target -> Parent -> ... -> Root のBubble方式です。
        // callback内で現在Elementや祖先SubtreeがRemoveされた場合、そのScopeはdispatch完了まで保持します。
        // またParent変更が起きたEventを新しいTreeへ跨いでBubbleさせないよう、callback前後の所属を確認します。
        UIElement* current = routeTarget;
        while (current != nullptr)
        {
            UIElement* parentBeforeDispatch = current->GetParent();
            event.CurrentTarget = current;
            current->HandleMouseEvent(event);

            if (event.Handled == true)
            {
                break;
            }

            if (current->m_Context != this)
            {
                break;
            }

            if (current->GetParent() != parentBeforeDispatch)
            {
                break;
            }

            current = parentBeforeDispatch;
        }
        handled = event.Handled;

        // MouseUpのHandlerはPressedTargetを参照するため、Routing完了後に状態を解除します。
        // 削除callbackで既に解除済みでもUpdatePressedTarget(nullptr)は安全に再実行できます。
        if (type == UIMouseEventType::Up && button == UIMouseButton::Left)
        {
            UpdatePressedTarget(nullptr);
        }

        EndMouseDispatch();
        return handled;
    }

    // Hit先がnullptrでもMouseUpだけはPressedを必ず解除し、UI外で離した場合の状態残留を防ぎます。
    if (type == UIMouseEventType::Up && button == UIMouseButton::Left)
    {
        UpdatePressedTarget(nullptr);
    }

    return false;
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

    // 別ContextやTree未所属ElementをCaptureすると、そのElement破棄をこのContextが観測できません。
    // Lifetime安全性を保証するため、Capture対象は必ずこのRetained Tree所属に限定します。
    if (element->m_Context != this)
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

void UIContext::CancelMouseCapture()
{
    UIElement* captureTarget = m_MouseCaptureElement;
    if (captureTarget == nullptr)
    {
        // Captureが無い場合でも、Mouse Upを失った経路でPressedだけが残っている可能性があります。
        UpdatePressedTarget(nullptr);
        return;
    }

    // Cancel Handler自身が新しいCaptureを要求した場合に古い所有権が邪魔をしないよう、
    // Event配送より先にContext側のCapture所有権を解除します。
    m_MouseCaptureElement = nullptr;

    UIMouseEvent event;
    event.Type = UIMouseEventType::Cancel;
    event.Button = UIMouseButton::None;
    event.Context = this;
    event.Target = captureTarget;
    event.PressedTarget = m_PressedElement;

    BeginMouseDispatch();

    // 通常のMouse Eventと同じTarget -> ParentのBubble規則でCancelを通知します。
    // Cancel callback自身がTree Mutationを起こす場合も通常Routingと同じ寿命保護を適用します。
    UIElement* current = captureTarget;
    while (current != nullptr)
    {
        UIElement* parentBeforeDispatch = current->GetParent();
        event.CurrentTarget = current;
        current->HandleMouseEvent(event);

        if (event.Handled == true)
        {
            break;
        }

        if (current->m_Context != this)
        {
            break;
        }

        if (current->GetParent() != parentBeforeDispatch)
        {
            break;
        }

        current = parentBeforeDispatch;
    }

    // Mouse Upが届かない異常終了経路ではPressedも残留し得るため、Captureと同じ境界で必ず解除します。
    UpdatePressedTarget(nullptr);
    EndMouseDispatch();
}

void UIContext::ReleaseMouseCapture()
{
    // 所有者を指定しない解除は「正常なDrag完了」ではなく強制終了として扱います。
    // WidgetへCancelを通知することで、Capture Pointerだけ消えてWidgetのDraggingだけ残る状態を防ぎます。
    CancelMouseCapture();
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

void UIContext::BeginMouseDispatch()
{
    ++m_MouseDispatchDepth;
}

void UIContext::EndMouseDispatch()
{
    if (m_MouseDispatchDepth == 0u)
    {
        return;
    }

    --m_MouseDispatchDepth;
    if (m_MouseDispatchDepth == 0u)
    {
        // 最外層Event callback / Subtree cleanupが完全に戻った後で初めて削除対象を破棄します。
        // これによりHandler自身がRemoveされた後も、Handlerの残りの処理とContext側cleanupを安全に完了できます。
        m_DeferredDestroyedSubtrees.clear();
    }
}

bool UIContext::IsMouseDispatchActive() const
{
    return m_MouseDispatchDepth > 0u;
}

void UIContext::RetainRemovedSubtree(Scope<UIElement> subtree)
{
    if (subtree == nullptr)
    {
        return;
    }

    if (IsMouseDispatchActive() == true)
    {
        m_DeferredDestroyedSubtrees.push_back(std::move(subtree));
    }
    // dispatch外では引数Scopeをこの関数の終了時にそのまま破棄します。
}

void UIContext::OnSubtreeRemoving(UIElement* subtreeRoot)
{
    if (subtreeRoot == nullptr)
    {
        return;
    }

    // Capture Cancel callbackが同じSubtreeや祖先をRemoveする再入ケースでも、
    // このcleanupがsubtreeRootを使い終えるまで実体を破棄させないため寿命保護区間へ入ります。
    BeginMouseDispatch();

    // Capture対象が破棄Subtree内なら、Elementが生存してParent chainも接続された状態でCancelを送ります。
    // 先にScopeを破棄するとUISlider/UISplitterのDraggingを終了できず、raw pointerもdanglingになります。
    if (IsElementInSubtree(m_MouseCaptureElement, subtreeRoot) == true)
    {
        CancelMouseCapture();
    }

    // Captureが無いHover/Pressed要素もContextがraw pointerで保持するため、破棄前に状態を解除します。
    if (IsElementInSubtree(m_HoveredElement, subtreeRoot) == true)
    {
        UpdateHoverTarget(nullptr);
    }

    if (IsElementInSubtree(m_PressedElement, subtreeRoot) == true)
    {
        UpdatePressedTarget(nullptr);
    }

    EndMouseDispatch();
}

bool UIContext::IsElementInSubtree(const UIElement* element, const UIElement* subtreeRoot)
{
    if (element == nullptr || subtreeRoot == nullptr)
    {
        return false;
    }

    // Parent chainを辿ることでSubtree全体を走査せず所属判定できます。
    // Treeから切り離す前に呼ぶことが前提なので、DescendantからsubtreeRootまでのchainは必ず維持されています。
    const UIElement* current = element;
    while (current != nullptr)
    {
        if (current == subtreeRoot)
        {
            return true;
        }

        current = current->GetParent();
    }

    return false;
}

} // namespace Raven
