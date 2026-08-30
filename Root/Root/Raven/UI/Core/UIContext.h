#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Core/UIEvent.h"
#include "Raven/UI/Rendering/UIRenderer.h"

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
//
// Interaction StateとしてHover / Pressed / Mouse CaptureもContext単位で管理します。
// Windowや描画TargetごとにContextを分離した場合でも、入力状態が別Contextへ漏れない構造を維持します。
class UIContext
{
public:
    UIContext();

    void BeginFrame(const math::Vec2& viewportSize);
    void EndFrame();

    // Mouse入力をHit Testし、Hover / Pressedを更新してから最前面TargetからRoot方向へBubbleさせます。
    // Interaction StateはUIContextが一元管理し、WidgetはUIElement上の状態を参照して見た目やClick判定へ利用します。
    // Capture中は物理的なHit先とは別にCapture ElementへEventを配送します。
    // これによりSliderやWindow Dragのように、PointerがElement外へ出ても継続すべき操作を実装できます。
    bool RouteMouseEvent(
        UIMouseEventType type,
        const math::Vec2& screenPosition,
        UIMouseButton button = UIMouseButton::None);

    bool RouteMouseMove(const math::Vec2& screenPosition);
    bool RouteMouseDown(const math::Vec2& screenPosition, UIMouseButton button);
    bool RouteMouseUp(const math::Vec2& screenPosition, UIMouseButton button);

    // 明示的なMouse Captureです。
    // 同一Elementからの再Captureは成功として扱い、別ElementがCapture中の場合は所有権を奪いません。
    // Widget間で暗黙にCaptureが移るとDrag中の操作対象が変わるため、Release後に改めてCaptureする設計とします。
    bool CaptureMouse(UIElement* element);
    void ReleaseMouseCapture(UIElement* element);
    void ReleaseMouseCapture();
    bool HasMouseCapture() const;
    bool HasMouseCapture(const UIElement* element) const;
    UIElement* GetMouseCaptureElement();
    const UIElement* GetMouseCaptureElement() const;

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
    void UpdateHoverTarget(UIElement* target);
    void UpdatePressedTarget(UIElement* target);

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
