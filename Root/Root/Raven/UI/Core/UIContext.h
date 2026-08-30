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

    // Mouse入力をHit Testし、Hover / Pressedを更新してから最前面TargetからRoot方向へBubbleさせます。
    // Interaction StateはUIContextが一元管理し、WidgetはUIElement上の状態を参照して見た目やClick判定へ利用します。
    bool RouteMouseEvent(
        UIMouseEventType type,
        const math::Vec2& screenPosition,
        UIMouseButton button = UIMouseButton::None)
    {
        if (m_RootElement == nullptr)
        {
            return false;
        }

        // Hit TestはArrange済みのPosition / Sizeを参照するため、Dirtyなら入力処理前にLayoutを確定します。
        if (m_RootElement->IsMeasureDirty() == true || m_RootElement->IsArrangeDirty() == true)
        {
            UIDrawList layoutResolveDrawList;
            m_RootElement->BuildDrawList(layoutResolveDrawList);
        }

        UIElement* target = UIHitTest::FindTopmost(*m_RootElement, screenPosition);
        UpdateHoverTarget(target);

        // MouseUpではPressedを解除する前のElementをEventへ保存します。
        // これによりButtonは「Down開始Element == Up時Hit Element」を安全に比較できます。
        UIElement* pressedTargetForEvent = m_PressedElement;

        if (type == UIMouseEventType::Down && button == UIMouseButton::Left)
        {
            UpdatePressedTarget(target);
            pressedTargetForEvent = m_PressedElement;
        }

        UIMouseEvent event;
        event.Type = type;
        event.Button = button;
        event.ScreenPosition = screenPosition;
        event.Target = target;
        event.PressedTarget = pressedTargetForEvent;

        bool handled = false;
        if (target != nullptr)
        {
            // Target -> Parent -> ... -> Root のBubble方式です。
            UIElement* current = target;
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
    bool m_FrameActive = false;
};

} // namespace Raven
