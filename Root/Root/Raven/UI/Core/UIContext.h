#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Core/UIEvent.h"
#include "Raven/UI/Core/UIHitTest.h"
#include "Raven/UI/Rendering/UIRenderer.h"

#include <utility>

namespace Raven
{

// ============================================================================
// UIContext
// ============================================================================
// 1つのUI描画対象に対するframe状態を管理します。
// Retained UI Treeと描画用DrawListに加え、Hover / Pressed / Mouse CaptureをContext単位で管理します。
class UIContext
{
public:
    UIContext()
        : m_RootElement(CreateScope<UIElement>())
    {
    }

    void BeginFrame(const math::Vec2& viewportSize)
    {
        m_DrawList.Clear();
        m_ViewportSize = viewportSize;
        m_FrameActive = true;
    }

    void EndFrame()
    {
        if (m_FrameActive == false)
        {
            return;
        }

        if (m_RootElement != nullptr)
        {
            m_RootElement->BuildDrawList(m_DrawList);
        }

        if (m_Renderer != nullptr)
        {
            m_Renderer->Render(m_DrawList, m_ViewportSize);
        }

        m_FrameActive = false;
    }

    // Mouse入力をHit Testし、Hover / Pressedを更新してからBubble Routingします。
    // Capture中は物理的なHit先とは別にCapture ElementへEventを配送します。
    // これによりSliderやWindow Dragのように、PointerがElement外へ出ても継続すべき操作を実装できます。
    bool RouteMouseEvent(
        UIMouseEventType type,
        const math::Vec2& screenPosition,
        UIMouseButton button = UIMouseButton::None)
    {
        if (m_RootElement == nullptr)
        {
            return false;
        }

        if (m_RootElement->IsMeasureDirty() == true || m_RootElement->IsArrangeDirty() == true)
        {
            UIDrawList layoutResolveDrawList;
            m_RootElement->BuildDrawList(layoutResolveDrawList);
        }

        // Hoverは常に実際のPointer位置を表す必要があるため、Capture中でもHit Test結果から更新します。
        UIElement* hitTarget = UIHitTest::FindTopmost(*m_RootElement, screenPosition);
        UpdateHoverTarget(hitTarget);

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
        event.Target = routeTarget;
        event.PressedTarget = pressedTargetForEvent;

        bool handled = false;
        if (routeTarget != nullptr)
        {
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

        if (type == UIMouseEventType::Up && button == UIMouseButton::Left)
        {
            UpdatePressedTarget(nullptr);
        }

        return handled;
    }

    bool RouteMouseMove(const math::Vec2& screenPosition)
    {
        return RouteMouseEvent(UIMouseEventType::Move, screenPosition, UIMouseButton::None);
    }

    bool RouteMouseDown(const math::Vec2& screenPosition, UIMouseButton button)
    {
        return RouteMouseEvent(UIMouseEventType::Down, screenPosition, button);
    }

    bool RouteMouseUp(const math::Vec2& screenPosition, UIMouseButton button)
    {
        return RouteMouseEvent(UIMouseEventType::Up, screenPosition, button);
    }

    // 明示的なMouse Captureです。
    // 同一Elementからの再Captureは成功として扱い、別ElementがCapture中の場合は所有権を奪いません。
    // Widget間で暗黙にCaptureが移るとDrag中の操作対象が変わるため、Release後に改めてCaptureする設計とします。
    bool CaptureMouse(UIElement* element)
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

    void ReleaseMouseCapture(UIElement* element)
    {
        // Capture所有者だけが解除できます。
        // 他Widgetが誤って現在のDrag操作を終了させることを防ぎます。
        if (element == nullptr || m_MouseCaptureElement != element)
        {
            return;
        }

        m_MouseCaptureElement = nullptr;
    }

    void ReleaseMouseCapture()
    {
        m_MouseCaptureElement = nullptr;
    }

    bool HasMouseCapture() const
    {
        return m_MouseCaptureElement != nullptr;
    }

    bool HasMouseCapture(const UIElement* element) const
    {
        return element != nullptr && m_MouseCaptureElement == element;
    }

    UIElement* GetMouseCaptureElement() { return m_MouseCaptureElement; }
    const UIElement* GetMouseCaptureElement() const { return m_MouseCaptureElement; }

    void SetRenderer(Scope<UIRenderer> renderer)
    {
        m_Renderer = std::move(renderer);
    }

    UIElement& GetRootElement() { return *m_RootElement; }
    const UIElement& GetRootElement() const { return *m_RootElement; }

    UIElement* GetHoveredElement() { return m_HoveredElement; }
    const UIElement* GetHoveredElement() const { return m_HoveredElement; }
    UIElement* GetPressedElement() { return m_PressedElement; }
    const UIElement* GetPressedElement() const { return m_PressedElement; }

    UIDrawList& GetDrawList() { return m_DrawList; }
    const UIDrawList& GetDrawList() const { return m_DrawList; }

    const math::Vec2& GetViewportSize() const { return m_ViewportSize; }
    bool IsFrameActive() const { return m_FrameActive; }

private:
    void UpdateHoverTarget(UIElement* target)
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

    void UpdatePressedTarget(UIElement* target)
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

private:
    math::Vec2 m_ViewportSize{};
    UIDrawList m_DrawList;
    Scope<UIElement> m_RootElement;
    Scope<UIRenderer> m_Renderer;
    UIElement* m_HoveredElement = nullptr;
    UIElement* m_PressedElement = nullptr;
    UIElement* m_MouseCaptureElement = nullptr;
    bool m_FrameActive = false;
};

} // namespace Raven
