#pragma once

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Core/UIElement.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

namespace Raven
{

// OS WindowではなくUIContext内に配置する論理Windowです。
// Viewportへの移譲はMouse Event中にTreeを変更せず、通知先がFrame境界で処理します。
class UIWindow : public UIElement
{
public:
    using ViewportTransferHandler = std::function<void(UIWindow*, const math::Vec2&)>;

    UIWindow()
    {
        SetAffectsParentMeasure(false);
        SetClipChildren(true);
        SetMinSize(math::Vec2(120.0f, 64.0f));
        SetPreferredSize(math::Vec2(320.0f, 240.0f));
    }

    void SetTitle(std::string title) { m_Title = std::move(title); }
    const std::string& GetTitle() const { return m_Title; }
    void SetOnViewportTransferRequested(ViewportTransferHandler handler)
    {
        m_OnViewportTransferRequested = std::move(handler);
    }
    void SetTitleBarHeight(float height) { m_TitleBarHeight = std::max(0.0f, height); }
    bool IsMoving() const { return m_Operation == Operation::Move; }
    bool IsResizing() const { return m_Operation == Operation::Resize; }

protected:
    void OnMouseEvent(UIMouseEvent& event) override
    {
        if (event.Type == UIMouseEventType::Cancel)
        {
            m_Operation = Operation::None;
            event.Handled = true;
            return;
        }

        if (event.Type == UIMouseEventType::Down && event.Button == UIMouseButton::Left)
        {
            // 子Widgetの操作を奪わず、Window本体へのDownだけを移動/Resizeに使用します。
            if (event.Target != this)
            {
                return;
            }
            if (GetParent() != nullptr)
            {
                GetParent()->BringChildToFront(this);
            }
            math::Vec2 local;
            if (TryScreenToLocalPosition(event.ScreenPosition, local) == false)
            {
                return;
            }
            const math::Vec2 size = GetSize();
            const bool resize = local.x >= size.x - m_ResizeHandleSize &&
                local.y >= size.y - m_ResizeHandleSize;
            const bool move = local.y >= 0.0f && local.y < m_TitleBarHeight;
            if ((resize == true || move == true) && event.Context != nullptr &&
                event.Context->CaptureMouse(this) == true)
            {
                m_Operation = resize == true ? Operation::Resize : Operation::Move;
                m_LastPointer = event.ScreenPosition;
                event.Handled = true;
            }
            return;
        }

        if (m_Operation == Operation::None || event.Target != this)
        {
            return;
        }
        if (event.Type == UIMouseEventType::Move ||
            (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left))
        {
            if (event.Context == nullptr || event.Context->HasMouseCapture(this) == false)
            {
                m_Operation = Operation::None;
                return;
            }
            const math::Vec2 delta = event.ScreenPosition - m_LastPointer;
            m_LastPointer = event.ScreenPosition;
            if (m_Operation == Operation::Move)
            {
                SetPosition(GetPosition() + delta);
            }
            else
            {
                const math::Vec2 size = GetSize();
                const math::Vec2 resized(
                    std::max(120.0f, size.x + delta.x),
                    std::max(64.0f, size.y + delta.y));
                SetPreferredSize(resized);
                SetSize(resized);
            }
            if (event.Type == UIMouseEventType::Up)
            {
                const bool moved = m_Operation == Operation::Move;
                m_Operation = Operation::None;
                event.Context->ReleaseMouseCapture(this);
                const math::Vec2 viewport = event.Context->GetViewportSize();
                const bool outside = event.ScreenPosition.x < 0.0f ||
                    event.ScreenPosition.y < 0.0f ||
                    event.ScreenPosition.x >= viewport.x ||
                    event.ScreenPosition.y >= viewport.y;
                // 通知中にDetachするとRoutingのParent chainが無効化されるため、
                // Callbackは要求を記録するだけにし、実際の移譲はFrame境界で行います。
                if (moved == true && outside == true &&
                    m_OnViewportTransferRequested != nullptr)
                {
                    m_OnViewportTransferRequested(this, event.ScreenPosition);
                }
            }
            event.Handled = true;
        }
    }

    void OnBuildDrawList(UIDrawList& drawList,
        const math::Vec2& absolutePosition) const override
    {
        const math::Vec2 size = GetSize();
        if (size.x <= 0.0f || size.y <= 0.0f)
        {
            return;
        }
        drawList.AddRect(absolutePosition, absolutePosition + size,
            ApplyVisualColor(math::Vec4(0.12f, 0.14f, 0.18f, 1.0f)));
        drawList.AddRect(absolutePosition,
            absolutePosition + math::Vec2(size.x, std::min(size.y, m_TitleBarHeight)),
            ApplyVisualColor(math::Vec4(0.20f, 0.24f, 0.32f, 1.0f)));
        drawList.AddRect(
            absolutePosition + math::Vec2(std::max(0.0f, size.x - m_ResizeHandleSize),
                std::max(0.0f, size.y - m_ResizeHandleSize)),
            absolutePosition + size,
            ApplyVisualColor(math::Vec4(0.38f, 0.44f, 0.55f, 1.0f)));
    }

private:
    enum class Operation { None, Move, Resize };
    std::string m_Title;
    ViewportTransferHandler m_OnViewportTransferRequested;
    math::Vec2 m_LastPointer{};
    float m_TitleBarHeight = 28.0f;
    float m_ResizeHandleSize = 16.0f;
    Operation m_Operation = Operation::None;
};

} // namespace Raven
