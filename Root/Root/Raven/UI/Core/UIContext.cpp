#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Core/UIHitTest.h"
#include "Raven/UI/Widgets/UITooltip.h"
#include "Raven/UI/Text/UIUtf8.h"

#include <utility>
#include <algorithm>

namespace Raven
{

UIContext::UIContext()
    : m_RootElement(CreateScope<UIElement>())
{
    // Rootから追加される全ElementへContext所属を伝播し、Tree変更時にInteraction Stateを安全に掃除できるようにします。
    m_RootElement->SetContextRecursive(this);
    // Layer自体はHit対象にならず、Popupだけを前面で描画・Hit Testします。
    auto layer = CreateScope<UIElement>();
    layer->SetAffectsParentMeasure(false);
    m_PopupLayer = m_RootElement->AddChild(std::move(layer));
}

void UIContext::CancelIMEComposition(UIElement* element)
{
    if (element == nullptr || element->HasActiveIMEComposition() == false)
    {
        return;
    }

    // OS側を先に取消し、同期的に返るEND通知も生存中のWidgetへ配送します。
    // OSがENDを返さない場合もUIの一時状態を必ず消去します。
    if (m_IMECancelCallback)
    {
        m_IMECancelCallback();
    }
    if (element->HasActiveIMEComposition() == true)
    {
        UIIMEEvent cancel;
        cancel.Type = UIIMEEventType::Cancel;
        cancel.Context = this;
        element->HandleIMEEvent(cancel);
    }
}

void UIContext::BeginFrame(const math::Vec2& viewportSize)
{
    // 前frameのDrawCommandを必ず破棄してから新しいframeを開始します。
    // UI TreeはRetained Modeとして保持しますが、DrawListはViewport/Layout結果から
    // 毎frame再構築することでResizeやStyle変更を即座に反映できるようにします。
    m_DrawList.Clear();
    m_ViewportSize = viewportSize;
    m_FrameActive = true;
    UpdateTooltip();
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
    UpdateTooltip();
    if (m_DragActive == true)
    {
        const auto now = std::chrono::steady_clock::now();
        // 停止・デバッグ復帰後の巨大なDeltaで一気にスクロールしないよう上限を設けます。
        const float deltaSeconds = std::min(
            std::chrono::duration<float>(now - m_LastDragTick).count(), 0.05f);
        m_LastDragTick = now;
        TickDrag(std::max(0.0f, deltaSeconds));
    }
    if (m_RootElement != nullptr)
    {
        m_RootElement->BuildDrawList(m_DrawList);
    }

    // Drag Previewは通常のTree描画後に追加し、WidgetのClip/Transformを継承しません。
    // Hit Test対象となるUIElementは生成せず、Capture中のDrop先探索を妨げません。
    if (m_DragActive == true)
    {
        const math::Vec2 min(m_LastPointerPosition.x + 12.0f,
            m_LastPointerPosition.y + 12.0f);
        float width = 112.0f;
        if (m_DragPreviewFont != nullptr && m_DragPreviewText.empty() == false)
        {
            // UTF-8をCodepoint単位で計測し、非ASCII名もbyte数で幅を誤算しません。
            float textWidth = 0.0f;
            std::size_t offset = 0u;
            std::uint32_t codepoint = 0u;
            while (UIUtf8::DecodeNext(m_DragPreviewText, offset, codepoint))
            {
                const UIGlyphMetrics* glyph = m_DragPreviewFont->FindGlyph(codepoint);
                textWidth += glyph != nullptr ? glyph->Advance : 12.0f;
            }
            width = std::max(width, textWidth + 24.0f);
        }
        const math::Vec2 max(min.x + width, min.y + 28.0f);
        m_DrawList.AddRect(min, max,
            m_DropTarget != nullptr
                ? math::Vec4(0.22f, 0.52f, 0.34f, 0.82f)
                : math::Vec4(0.34f, 0.37f, 0.44f, 0.76f));
        // 左端のAccentで「受入可能 / 不可」を区別します。
        m_DrawList.AddRect(min, math::Vec2(min.x + 4.0f, max.y),
            m_DropTarget != nullptr
                ? math::Vec4(0.42f, 0.90f, 0.57f, 0.95f)
                : math::Vec4(0.85f, 0.67f, 0.36f, 0.95f));
        if (m_DragPreviewFont != nullptr && m_DragPreviewText.empty() == false)
        {
            m_DragPreviewFont->AppendText(m_DrawList, m_DragPreviewText,
                math::Vec2(min.x + 12.0f, min.y + 20.0f), 28.0f,
                math::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
        }
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
    UIMouseButton button,
    const math::Vec2& scrollDelta)
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

    m_LastPointerPosition = screenPosition;
    if (type == UIMouseEventType::Down || type == UIMouseEventType::Scroll)
    {
        HideTooltip();
        m_TooltipSuppressedUntilMove = true;
    }
    if (type == UIMouseEventType::Move && m_TooltipSuppressedUntilMove)
    {
        m_TooltipSuppressedUntilMove = false;
        m_HoverStarted = std::chrono::steady_clock::now();
    }

    // 外側Downを消費し、閉じた直後に背面Buttonを誤操作しないようにします。
    if (m_OpenPopup != nullptr && type == UIMouseEventType::Down)
    {
        UIElement* popupHit = UIHitTest::FindTopmost(*m_RootElement, screenPosition);
        if (IsElementInSubtree(popupHit, m_OpenPopup) == false)
        {
            ClosePopup();
            UpdateHoverTarget(nullptr);
            UpdatePressedTarget(nullptr);
            return true;
        }
    }

    // Hoverは常に実際のPointer位置を表す必要があるため、Capture中でもHit Test結果から更新します。
    UIElement* hitTarget = UIHitTest::FindTopmost(*m_RootElement, screenPosition);
    UpdateHoverTarget(hitTarget);
    if (type == UIMouseEventType::Move)
    {
        UpdateTooltip();
    }

    if (type == UIMouseEventType::Down && button == UIMouseButton::Left)
    {
        UIElement* focused = GetFocusedElement();
        // 別Widgetや背景をクリックした時点で編集を確定します。
        // Focus対象自身の子をクリックした場合はFocusを維持します。
        if (focused != nullptr && IsElementInSubtree(hitTarget, focused) == false)
        {
            ClearFocus();
        }
    }

    // Pressedは「どのElement上で押し始めたか」を保持する状態です。
    // MouseUpでは解除前のElementをEventへ保存し、ButtonがDown開始ElementとUp時Targetを比較できるようにします。
    UIElement* pressedTargetForEvent = m_PressedElement;
    if (type == UIMouseEventType::Down && button == UIMouseButton::Left)
    {
        UpdatePressedTarget(hitTarget);
        pressedTargetForEvent = m_PressedElement;
    }

    // Scrollは現在Pointer下にあるScroll Containerへ渡す入力なので、Drag Captureとは独立させます。
    // これによりSliderをCapture中でも、Wheel自体は見えているScrollViewの階層からBubbleできます。
    UIElement* routeTarget = nullptr;
    if (type == UIMouseEventType::Scroll)
    {
        routeTarget = hitTarget;
    }
    else
    {
        routeTarget = m_MouseCaptureElement;
        if (routeTarget == nullptr)
        {
            routeTarget = hitTarget;
        }
    }

    UIMouseEvent event;
    event.Type = type;
    event.Button = button;
    event.ScreenPosition = screenPosition;
    event.ScrollDelta = scrollDelta;
    event.Context = this;
    event.Target = routeTarget;
    event.PressedTarget = pressedTargetForEvent;

    bool handled = false;
    // Drag確定時のUpをButton等へ配送すると、Dropと同時にClickが発火してしまいます。
    // Drag SourceはEnd/Cancel通知で操作状態を片付けます。
    const bool suppressMouseUp = type == UIMouseEventType::Up &&
        button == UIMouseButton::Left && m_DragActive == true;
    if (routeTarget != nullptr && suppressMouseUp == false)
    {
        // Target -> Parent -> ... -> Root のBubble方式です。
        // Scrollも同じ規則を使うため、内側ScrollViewが境界で消費できない場合に外側ScrollViewへ自然に伝播できます。
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

    // Captureによる入力配送とDrop先のHit Testは独立させます。
    // 通常のMouse HandlerがBeginDragを呼ぶため、Drag更新はRoutingの後に実行します。
    if (type == UIMouseEventType::Move && m_DragSource != nullptr)
    {
        UpdateDrag(screenPosition, hitTarget);
        handled = handled || m_DragActive;
    }
    if (type == UIMouseEventType::Up && button == UIMouseButton::Left && m_DragSource != nullptr)
    {
        const bool wasActive = m_DragActive;
        FinishDrag(screenPosition, hitTarget);
        handled = handled || wasActive;
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

bool UIContext::RouteMouseScroll(
    const math::Vec2& screenPosition,
    const math::Vec2& scrollDelta)
{
    return RouteMouseEvent(
        UIMouseEventType::Scroll,
        screenPosition,
        UIMouseButton::None,
        scrollDelta);
}

bool UIContext::BeginDrag(UIElement* source, UIDragDropPayload payload, const math::Vec2& startPosition)
{
    if (source == nullptr || source->GetContext() != this ||
        m_DragSource != nullptr || payload.Type.empty())
    {
        return false;
    }
    // 他WidgetのCaptureを奪わず、Source自身が入力を継続受信します。
    if (CaptureMouse(source) == false)
    {
        return false;
    }
    m_DragPreviewText.clear();
    m_DragPreviewFont = nullptr;
    m_DragSource = source;
    m_DragPayload = std::move(payload);
    m_DragStart = startPosition;
    m_DragActive = false;
    m_DropTarget = nullptr;
    m_LastDragTick = std::chrono::steady_clock::now();
    return true;
}

bool UIContext::IsLiveDragElement(const UIElement* element) const
{
    if (element == nullptr || m_RootElement == nullptr)
    {
        return false;
    }
    // callbackがElementを削除し得るため、対象を逆参照せず所有Treeからアドレスを照合します。
    std::function<bool(const UIElement*)> contains = [&](const UIElement* current)
    {
        if (current == element)
        {
            return true;
        }
        for (const auto& child : current->GetChildren())
        {
            if (child != nullptr && contains(child.get()) == true)
            {
                return true;
            }
        }
        return false;
    };
    return contains(m_RootElement.get());
}

void UIContext::SetDragPreview(std::string text, const Ref<UIFontAtlas>& font)
{
    if (m_DragSource == nullptr)
    {
        return;
    }
    m_DragPreviewText = std::move(text);
    m_DragPreviewFont = font;
}

void UIContext::SendDragEvent(UIElement* element, UIDragDropEventType type, const math::Vec2& position)
{
    if (IsLiveDragElement(element) == false)
    {
        return;
    }
    UIDragDropEvent event;
    event.Type = type;
    event.Payload = &m_DragPayload;
    event.Source = m_DragSource;
    event.ScreenPosition = position;
    element->HandleDragDropEvent(event);
}

void UIContext::UpdateDrag(const math::Vec2& position, UIElement* hitTarget, float deltaSeconds)
{
    if (m_DragSource == nullptr)
    {
        return;
    }
    if (m_DragActive == false)
    {
        const float dx = position.x - m_DragStart.x;
        const float dy = position.y - m_DragStart.y;
        if (dx * dx + dy * dy < m_DragThreshold * m_DragThreshold)
        {
            return;
        }
        m_DragActive = true;
        m_LastDragTick = std::chrono::steady_clock::now();
        HideTooltip();
        SendDragEvent(m_DragSource, UIDragDropEventType::Begin, position);
        if (m_DragSource == nullptr)
        {
            return;
        }
    }

    // callbackが現在の候補を削除してもParentを逆参照しないよう、経路を先に保存します。
    std::vector<UIElement*> candidates;
    for (UIElement* current = hitTarget; current != nullptr; current = current->GetParent())
    {
        candidates.push_back(current);
    }
    UIElement* accepted = nullptr;
    for (UIElement* candidate : candidates)
    {
        if (IsLiveDragElement(candidate) == false)
        {
            continue;
        }
        UIDragDropEvent event;
        event.Type = UIDragDropEventType::Over;
        event.DeltaSeconds = deltaSeconds;
        event.Payload = &m_DragPayload;
        event.Source = m_DragSource;
        event.ScreenPosition = position;
        const bool acceptedHere = candidate->HandleDragDropEvent(event);
        if (m_DragSource == nullptr)
        {
            return;
        }
        if ((acceptedHere == true || event.Accepted == true) &&
            IsLiveDragElement(candidate) == true)
        {
            accepted = candidate;
            break;
        }
    }
    if (accepted != m_DropTarget)
    {
        UIElement* previous = m_DropTarget;
        // Leave中の再入処理が古いTargetを再利用しないよう先に解除します。
        m_DropTarget = nullptr;
        SendDragEvent(previous, UIDragDropEventType::Leave, position);
        if (m_DragSource == nullptr)
        {
            return;
        }
        if (IsLiveDragElement(accepted) == true)
        {
            m_DropTarget = accepted;
            SendDragEvent(accepted, UIDragDropEventType::Enter, position);
        }
    }
}

void UIContext::TickDrag(float deltaSeconds)
{
    if (m_DragActive == false || m_RootElement == nullptr ||
        std::isfinite(deltaSeconds) == false || deltaSeconds <= 0.0f)
    {
        return;
    }
    // 自動スクロールによって表示行が変わるため、毎TickでDrop先を再判定します。
    UIElement* hitTarget = UIHitTest::FindTopmost(*m_RootElement, m_LastPointerPosition);
    UpdateDrag(m_LastPointerPosition, hitTarget, deltaSeconds);
}

void UIContext::FinishDrag(const math::Vec2& position, UIElement* hitTarget)
{
    // Upだけで閾値を越えた場合はDragを新規成立させず、Click扱いを維持します。
    // Drag成立はMoveでのみ判定し、Upでは既存のDrop先を最終更新します。
    if (m_DragActive == true)
    {
        UpdateDrag(position, hitTarget);
    }
    if (m_DragSource == nullptr)
    {
        return;
    }

    // Drop先のcallbackがSource/Targetを削除できるので、セッションを先に終了し、
    // Eventに渡すPayloadだけをローカルへ移して寿命を確保します。
    UIElement* source = m_DragSource;
    UIElement* target = m_DragActive == true ? m_DropTarget : nullptr;
    UIDragDropPayload payload = std::move(m_DragPayload);
    const bool wasActive = m_DragActive;
    m_DragSource = nullptr;
    m_DropTarget = nullptr;
    m_DragActive = false;
    m_DragPayload = {};
    m_DragPreviewText.clear();
    m_DragPreviewFont = nullptr;
    ReleaseMouseCapture(source);

    UIDragDropEvent event;
    event.Payload = &payload;
    event.Source = source;
    event.ScreenPosition = position;
    if (wasActive == true && IsLiveDragElement(target) == true)
    {
        event.Type = UIDragDropEventType::Drop;
        target->HandleDragDropEvent(event);
    }
    if (wasActive == true && IsLiveDragElement(source) == true)
    {
        event.Type = UIDragDropEventType::End;
        source->HandleDragDropEvent(event);
    }
}

void UIContext::CancelDrag()
{
    if (m_DragSource == nullptr)
    {
        return;
    }
    // Cancel/Leave callbackがTreeを変更しても再帰Cancelせず、削除済み要素へ通知しません。
    UIElement* source = m_DragSource;
    UIElement* target = m_DropTarget;
    UIDragDropPayload payload = std::move(m_DragPayload);
    m_DragSource = nullptr;
    m_DropTarget = nullptr;
    m_DragActive = false;
    m_DragPayload = {};
    m_DragPreviewText.clear();
    m_DragPreviewFont = nullptr;
    ReleaseMouseCapture(source);

    UIDragDropEvent event;
    event.Payload = &payload;
    event.Source = source;
    event.ScreenPosition = m_LastPointerPosition;
    if (IsLiveDragElement(target) == true)
    {
        event.Type = UIDragDropEventType::Leave;
        target->HandleDragDropEvent(event);
    }
    if (IsLiveDragElement(source) == true)
    {
        event.Type = UIDragDropEventType::Cancel;
        source->HandleDragDropEvent(event);
    }
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
    CancelDrag();
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

    // 通常のMouse Eventと同じTarget -> ParentのBubble規則でCancelを通知します。
    // Drag Widget自身が処理しない場合でも、親Containerが必要に応じて操作中断を観測できます。
    UIElement* current = captureTarget;
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

    // Mouse Upが届かない異常終了経路ではPressedも残留し得るため、Captureと同じ境界で必ず解除します。
    UpdatePressedTarget(nullptr);
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

UIElement* UIContext::AddPopup(Scope<UIElement> popup)
{
    if (popup == nullptr || m_PopupLayer == nullptr)
    {
        return nullptr;
    }
    popup->SetVisible(false);
    popup->SetAffectsParentMeasure(false);
    return m_PopupLayer->AddChild(std::move(popup));
}

bool UIContext::RemovePopup(UIElement* popup)
{
    if (popup == nullptr || m_PopupLayer == nullptr || popup->GetParent() != m_PopupLayer)
    {
        return false;
    }
    // RemoveChildがOnSubtreeRemovingを通るため、開いているPopupも安全にCloseされます。
    return m_PopupLayer->RemoveChild(popup);
}

bool UIContext::OpenPopup(UIElement* popup)
{
    if (popup == nullptr || popup->GetParent() != m_PopupLayer)
    {
        return false;
    }
    if (m_OpenPopup == popup)
    {
        return true;
    }
    ClosePopup();
    HideTooltip();
    // Rootへ後から通常Widgetが追加されてもPopupが常に最前面になるよう、
    // Open時にLayerをPainter's Orderの末尾へ移します。
    m_RootElement->BringChildToFront(m_PopupLayer);
    m_OpenPopup = popup;
    popup->SetVisible(true);
    return true;
}

bool UIContext::OpenPopupAt(UIElement* popup, const UIElement* anchor, float gap)
{
    if (popup == nullptr || anchor == nullptr ||
        anchor->GetContext() != this || popup->GetParent() != m_PopupLayer)
    {
        return false;
    }

    const math::Vec2 topLeft = anchor->LocalToScreenPosition(math::Vec2(0.0f, 0.0f));
    const math::Vec2 bottomLeft = anchor->LocalToScreenPosition(
        math::Vec2(0.0f, anchor->GetSize().y));
    const math::Vec2 popupSize = popup->GetPreferredSize();
    const float viewportWidth = m_ViewportSize.x;
    const float viewportHeight = m_ViewportSize.y;
    float x = bottomLeft.x;
    float y = bottomLeft.y + gap;

    // 下側に収まらない場合はAnchorの上側を優先します。
    if (viewportHeight > 0.0f && y + popupSize.y > viewportHeight)
    {
        y = topLeft.y - gap - popupSize.y;
    }
    if (viewportWidth > 0.0f)
    {
        x = std::clamp(x, 0.0f, std::max(0.0f, viewportWidth - popupSize.x));
    }
    if (viewportHeight > 0.0f)
    {
        y = std::clamp(y, 0.0f, std::max(0.0f, viewportHeight - popupSize.y));
    }

    // Popup LayerはRootのAbsolute ChildなのでScreen座標からRoot原点を引きます。
    const math::Vec2 rootPosition = m_RootElement->LocalToScreenPosition(math::Vec2(0.0f, 0.0f));
    popup->SetPosition(math::Vec2(x - rootPosition.x, y - rootPosition.y));
    return OpenPopup(popup);
}

void UIContext::ClosePopup()
{
    UIElement* popup = m_OpenPopup;
    if (popup == nullptr)
    {
        return;
    }
    m_OpenPopup = nullptr;
    // 非表示になるWidgetのCaptureとFocusを先に解放します。
    if (IsElementInSubtree(m_MouseCaptureElement, popup))
    {
        CancelMouseCapture();
    }
    if (IsElementInSubtree(GetFocusedElement(), popup))
    {
        ClearFocus();
    }
    if (IsElementInSubtree(m_HoveredElement, popup))
    {
        UpdateHoverTarget(nullptr);
    }
    if (IsElementInSubtree(m_PressedElement, popup))
    {
        UpdatePressedTarget(nullptr);
    }
    popup->SetVisible(false);
}

bool UIContext::SetTooltip(UIElement* target, std::string text,
    const Ref<UIFontAtlas>& font, float delaySeconds)
{
    if (target == nullptr || target->GetContext() != this || text.empty() ||
        delaySeconds < 0.0f)
    {
        return false;
    }
    ClearTooltip(target);
    m_Tooltips.push_back({ target, std::move(text), font, delaySeconds });
    if (m_Tooltip == nullptr && m_PopupLayer != nullptr)
    {
        auto tooltip = CreateScope<UITooltip>();
        m_Tooltip = static_cast<UITooltip*>(m_PopupLayer->AddChild(std::move(tooltip)));
    }
    if (m_HoveredElement != nullptr && IsElementInSubtree(m_HoveredElement, target))
    {
        m_TooltipTarget = target;
        m_HoverStarted = std::chrono::steady_clock::now();
    }
    return m_Tooltip != nullptr;
}

bool UIContext::ClearTooltip(UIElement* target)
{
    if (target == nullptr)
    {
        return false;
    }
    const auto found = std::find_if(m_Tooltips.begin(), m_Tooltips.end(),
        [target](const TooltipRegistration& entry) { return entry.Target == target; });
    if (found == m_Tooltips.end())
    {
        return false;
    }
    if (m_TooltipTarget == target)
    {
        HideTooltip();
        m_TooltipTarget = nullptr;
    }
    m_Tooltips.erase(found);
    return true;
}

const UITooltip* UIContext::GetVisibleTooltip() const
{
    return m_Tooltip != nullptr && m_Tooltip->IsVisible() ? m_Tooltip : nullptr;
}

void UIContext::HideTooltip()
{
    if (m_Tooltip != nullptr && m_Tooltip->IsVisible())
    {
        m_Tooltip->SetVisible(false);
    }
}

void UIContext::UpdateTooltip()
{
    if (m_Tooltip == nullptr || m_TooltipTarget == nullptr || m_OpenPopup != nullptr ||
        m_TooltipSuppressedUntilMove)
    {
        HideTooltip();
        return;
    }
    const auto found = std::find_if(m_Tooltips.begin(), m_Tooltips.end(),
        [this](const TooltipRegistration& entry) { return entry.Target == m_TooltipTarget; });
    if (found == m_Tooltips.end())
    {
        HideTooltip();
        return;
    }
    const float elapsed = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - m_HoverStarted).count();
    if (elapsed < found->DelaySeconds)
    {
        HideTooltip();
        return;
    }
    m_Tooltip->SetContent(found->Text, found->Font);
    const math::Vec2 size = m_Tooltip->GetPreferredSize();
    float x = m_LastPointerPosition.x + 12.0f;
    float y = m_LastPointerPosition.y + 18.0f;
    if (m_ViewportSize.x > 0.0f)
    {
        x = std::clamp(x, 0.0f, std::max(0.0f, m_ViewportSize.x - size.x));
    }
    if (m_ViewportSize.y > 0.0f)
    {
        y = std::clamp(y, 0.0f, std::max(0.0f, m_ViewportSize.y - size.y));
    }
    const math::Vec2 origin = m_RootElement->LocalToScreenPosition(math::Vec2(0.0f, 0.0f));
    m_Tooltip->SetPosition(math::Vec2(x - origin.x, y - origin.y));
    m_RootElement->BringChildToFront(m_PopupLayer);
    m_Tooltip->SetVisible(true);
}

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
    HideTooltip();
    m_TooltipTarget = nullptr;
    for (const TooltipRegistration& entry : m_Tooltips)
    {
        if (IsElementInSubtree(target, entry.Target))
        {
            m_TooltipTarget = entry.Target;
            m_HoverStarted = std::chrono::steady_clock::now();
            break;
        }
    }
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

void UIContext::OnSubtreeRemoving(UIElement* subtreeRoot)
{
    if (subtreeRoot == nullptr)
    {
        return;
    }

    // 対象Treeが切り離される前にTooltip登録を解除し、dangling pointerを防ぎます。
    for (auto iterator = m_Tooltips.begin(); iterator != m_Tooltips.end();)
    {
        if (IsElementInSubtree(iterator->Target, subtreeRoot))
        {
            if (m_TooltipTarget == iterator->Target)
            {
                HideTooltip();
                m_TooltipTarget = nullptr;
            }
            iterator = m_Tooltips.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
    if (IsElementInSubtree(m_PopupLayer, subtreeRoot))
    {
        m_Tooltip = nullptr;
        m_TooltipTarget = nullptr;
    }

    // 外部からPopupを削除した場合も非所有Pointerを残しません。
    if (IsElementInSubtree(m_OpenPopup, subtreeRoot))
    {
        ClosePopup();
    }
    if (IsElementInSubtree(m_PopupLayer, subtreeRoot))
    {
        m_PopupLayer = nullptr;
    }

    // Widget破棄前にOSとUI双方のIME変換を破棄します。
    // Focusが属するSubtreeだけを対象とし、他の入力欄の変換は維持します。
    UIElement* focused = GetFocusedElement();
    if (IsElementInSubtree(focused, subtreeRoot) == true)
    {
        CancelIMEComposition(focused);
        ClearFocus();
    }

    // Source/TargetのどちらかがTreeから外れる前にDragを終了します。
    if (IsElementInSubtree(m_DragSource, subtreeRoot) == true ||
        IsElementInSubtree(m_DropTarget, subtreeRoot) == true)
    {
        CancelDrag();
    }

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
