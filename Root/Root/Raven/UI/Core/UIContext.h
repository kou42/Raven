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
//
// 現段階ではApplicationのMain Window用Contextとして利用しますが、Context自体を
// WindowやEditorへ直接依存させていません。このため将来は、
//   - Main Window上のEditor UI
//   - Game View / RenderTexture上のGame UI
//   - World Space UI用の別Context
// のように複数Contextへ拡張できます。
//
// UIContextはRetained UI Treeそのものではなく、「今frameの描画要求」を集約する境界です。
// UIElement / Layout / Event SystemはこのContextへUIDrawCommandを生成します。
//
// 現在は最初のRetained Mode基盤としてRoot UIElementも所有します。
// Root以下のElement Treeはframeを跨いで保持し、EndFrame()直前にLayoutを解決してUIDrawListへ展開します。
// これによりWidgetのLifetimeとGPUへ渡す一時DrawCommandのLifetimeを分離します。
class UIContext
{
public:
    UIContext()
        : m_RootElement(CreateScope<UIElement>())
    {
    }

    void BeginFrame(const math::Vec2& viewportSize)
    {
        // 前frameのDrawCommandを必ず破棄してから新しいframeを開始します。
        // UI TreeはRetained Modeとして保持しますが、DrawListはViewport/Layout結果から
        // 毎frame再構築することでResizeやStyle変更を即座に反映できるようにします。
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

        // Hit TestはArrange済みのPosition / Sizeを参照します。
        // 入力がEndFrame()より先に届いても古いLayoutを使わないよう、Dirty時はここでLayoutを解決します。
        // 現在のUIElementはLayout専用APIをまだ公開していないため、BuildDrawList()を一時Listに対して呼びます。
        // Layout APIをMeasure / Arrangeとして公開した段階で、この一時DrawListは不要になります。
        if (m_RootElement->IsMeasureDirty() == true || m_RootElement->IsArrangeDirty() == true)
        {
            UIDrawList layoutResolveDrawList;
            m_RootElement->BuildDrawList(layoutResolveDrawList);
        }

        UIElement* target = UIHitTest::FindTopmost(*m_RootElement, screenPosition);
        UpdateHoverTarget(target);

        // Pressedは「どのElement上で押し始めたか」を保持するCaptureに近い状態です。
        // MouseUp時のHit先が別Elementでも必ず解除することで、押下状態が残留するのを防ぎます。
        if (type == UIMouseEventType::Down && button == UIMouseButton::Left)
        {
            UpdatePressedTarget(target);
        }
        else if (type == UIMouseEventType::Up && button == UIMouseButton::Left)
        {
            UpdatePressedTarget(nullptr);
        }

        if (target == nullptr)
        {
            return false;
        }

        UIMouseEvent event;
        event.Type = type;
        event.Button = button;
        event.ScreenPosition = screenPosition;
        event.Target = target;

        // Target -> Parent -> ... -> Root のBubble方式です。
        // WidgetがHandledを立てた時点で親への伝播を止めます。
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

        return event.Handled;
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

    UIElement& GetRootElement()
    {
        return *m_RootElement;
    }

    const UIElement& GetRootElement() const
    {
        return *m_RootElement;
    }

    UIElement* GetHoveredElement() { return m_HoveredElement; }
    const UIElement* GetHoveredElement() const { return m_HoveredElement; }
    UIElement* GetPressedElement() { return m_PressedElement; }
    const UIElement* GetPressedElement() const { return m_PressedElement; }

    UIDrawList& GetDrawList()
    {
        return m_DrawList;
    }

    const UIDrawList& GetDrawList() const
    {
        return m_DrawList;
    }

    const math::Vec2& GetViewportSize() const
    {
        return m_ViewportSize;
    }

    bool IsFrameActive() const
    {
        return m_FrameActive;
    }

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
