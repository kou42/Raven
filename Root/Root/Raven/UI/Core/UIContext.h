#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Core/UIEvent.h"
#include "Raven/UI/Rendering/UIRenderer.h"

namespace Raven
{

class UIContext
{
public:
    UIContext();
    void BeginFrame(const math::Vec2& viewportSize);
    void EndFrame();

    bool RouteMouseEvent(UIMouseEventType type, const math::Vec2& screenPosition, UIMouseButton button = UIMouseButton::None, const math::Vec2& scrollDelta = math::Vec2{});
    bool RouteMouseMove(const math::Vec2& screenPosition);
    bool RouteMouseDown(const math::Vec2& screenPosition, UIMouseButton button);
    bool RouteMouseUp(const math::Vec2& screenPosition, UIMouseButton button);
    bool RouteMouseScroll(const math::Vec2& screenPosition, const math::Vec2& scrollDelta);

    bool CaptureMouse(UIElement* element);
    void ReleaseMouseCapture(UIElement* element);
    void CancelMouseCapture();
    void ReleaseMouseCapture();
    bool HasMouseCapture() const;
    bool HasMouseCapture(const UIElement* element) const;
    UIElement* GetMouseCaptureElement();
    const UIElement* GetMouseCaptureElement() const;

    // FocusはContext単位で1要素だけ保持します。Focusableでない要素や別Treeの要素は拒否します。
    bool SetFocus(UIElement* element);
    void ClearFocus();
    UIElement* GetFocusedElement();
    const UIElement* GetFocusedElement() const;

    // Treeの深さ優先順をTab Orderとして利用する最小Keyboard Navigationです。
    // reverse=trueでShift+Tab相当の逆方向へ移動します。
    bool MoveFocus(bool reverse = false);

    void SetRenderer(Scope<UIRenderer> renderer);
    UIElement& GetRootElement();
    const UIElement& GetRootElement() const;
    UIElement* GetHoveredElement();
    const UIElement* GetHoveredElement() const;
    UIElement* GetPressedElement();
    const UIElement* GetPressedElement() const;
    UIDrawList& GetDrawList();
    const UIDrawList& GetDrawList() const;
    const math::Vec2& GetViewportSize() const;
    bool IsFrameActive() const;

private:
    friend class UIElement;
    void UpdateHoverTarget(UIElement* target);
    void UpdatePressedTarget(UIElement* target);
    void CollectFocusableElements(UIElement* root, std::vector<UIElement*>& outElements) const;
    void OnSubtreeRemoving(UIElement* subtreeRoot);
    static bool IsElementInSubtree(const UIElement* element, const UIElement* subtreeRoot);

private:
    math::Vec2 m_ViewportSize{};
    UIDrawList m_DrawList;
    Scope<UIElement> m_RootElement;
    Scope<UIRenderer> m_Renderer;
    UIElement* m_HoveredElement = nullptr;
    UIElement* m_PressedElement = nullptr;
    UIElement* m_MouseCaptureElement = nullptr;
    UIElement* m_FocusedElement = nullptr;
    bool m_FrameActive = false;
};

} // namespace Raven
