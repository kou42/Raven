#pragma once

#include "Raven/Core/Base.h"
#include <functional>
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Core/UIEvent.h"
#include "Raven/UI/Style/UITheme.h"
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
// Interaction StateとしてHover / Pressed / Mouse Captureに加え、Keyboard FocusもContext単位で管理します。
// Windowや描画TargetごとにContextを分離した場合でも、入力状態が別Contextへ漏れない構造を維持します。
class UIFontAtlas;
class UITooltip;

class UIContext
{
public:
    UIContext();

    void BeginFrame(const math::Vec2& viewportSize);
    // Window単位でDPI・論理サイズ・実Pixelサイズを同じFrame境界に同期します。
    // 補助Windowもそれぞれ独立したUIContextに対してこの入口を使用します。
    void BeginFrame(const math::Vec2& viewportSize,
        const math::Vec2& framebufferSize, float dpiScaleX, float dpiScaleY);
    // Window論理サイズとは独立した実Pixel数を描画境界へ渡します。
    void SetFramebufferSize(const math::Vec2& framebufferSize)
    {
        m_FramebufferSize = framebufferSize;
        m_HasFramebufferSize = true;
    }
    void EndFrame();

    // GPU Contextが有効な描画準備段階で呼び、Tree内の未生成DPI Fontをまとめて解決します。
    // DPI通知やBeginFrameではGPU Contextが保証されないため自動生成しません。
    // 成功したLabel数を返します。失敗したLabelはPending状態を保持しつつ自動再試行を停止します。
    // Font復旧後はUILabel::RetryDPIFont()で再試行を明示してください。
    std::size_t RefreshPendingDPIFonts();
    std::size_t GetPendingDPIFontCount() const;

    // OS由来のDPI倍率とユーザー設定倍率を分離して保持します。
    // 現段階では既存のWindow座標/Layout/Hit Test/描画を変更せず、後続Phaseの基盤とします。
    void SetDPIScale(float x, float y);
    float GetDPIScaleX() const { return m_DPIScaleX; }
    float GetDPIScaleY() const { return m_DPIScaleY; }
    void SetUserScale(float scale);
    float GetUserScale() const { return m_UserScale; }
    float GetEffectiveScaleX() const { return m_DPIScaleX * m_UserScale; }
    float GetEffectiveScaleY() const { return m_DPIScaleY * m_UserScale; }

    // Window座標とDPI非依存のUI設計座標の変換をContextに集約します。
    // GetViewportSize()は既存互換のWindow論理座標のままです。
    math::Vec2 GetLayoutViewportSize() const;
    math::Vec2 WindowToLayoutPosition(const math::Vec2& windowPosition) const;
    math::Vec2 LayoutToWindowPosition(const math::Vec2& layoutPosition) const;

    // Mouse入力をHit Testし、Hover / Pressedを更新してから最前面TargetからRoot方向へBubbleさせます。
    // Interaction StateはUIContextが一元管理し、WidgetはUIElement上の状態を参照して見た目やClick判定へ利用します。
    // Capture中は物理的なHit先とは別にCapture ElementへEventを配送します。
    // ScrollはDrag Captureとは独立したPointer位置の操作なので、常に実際のHit TargetからBubbleさせます。
    bool RouteMouseEvent(
        UIMouseEventType type,
        const math::Vec2& screenPosition,
        UIMouseButton button = UIMouseButton::None,
        const math::Vec2& scrollDelta = math::Vec2{});

    bool RouteMouseMove(const math::Vec2& screenPosition);
    bool RouteMouseDown(const math::Vec2& screenPosition, UIMouseButton button);
    bool RouteMouseUp(const math::Vec2& screenPosition, UIMouseButton button);
    bool RouteMouseScroll(const math::Vec2& screenPosition, const math::Vec2& scrollDelta);

    // ApplicationでPlatform Key CodeをSemantic UIKeyへ変換した後のKeyboard Event入口です。
    // 現段階ではTab / Shift+TabをFocus Navigationとして扱い、RepeatではFocusを進めません。
    bool RouteKeyEvent(const UIKeyEvent& event);
    bool RouteCharacterEvent(std::uint32_t codepoint);
    // IME未確定状態と一括確定文字列をFocus所有Widgetへ配送します。通常文字はCharacter Eventです。
    bool RouteIMEEvent(const UIIMEEvent& event);

    // Keyboard / Gamepad Navigation対象のFocusをContext内で一意に管理します。
    // Focus状態はElement自身へ保持し、Contextは必要時にTreeを検索するためSubtree破棄でraw pointerを残しません。
    bool SetFocus(UIElement* element);
    void ClearFocus();
    // Focus/Tree/外部Text変更時にOS側の未確定変換を同期的に取り消します。
    void SetIMECancelCallback(std::function<void()> callback) { m_IMECancelCallback = std::move(callback); }
    void CancelIMEComposition(UIElement* element);
    UIElement* GetFocusedElement();
    const UIElement* GetFocusedElement() const;
    bool MoveFocus(bool reverse = false);

    // 明示的なMouse Captureです。
    // 同一Elementからの再Captureは成功として扱い、別ElementがCapture中の場合は所有権を奪いません。
    // Widget間で暗黙にCaptureが移るとDrag中の操作対象が変わるため、Release後に改めてCaptureする設計とします。
    bool CaptureMouse(UIElement* element);
    void ReleaseMouseCapture(UIElement* element);

    // Capture所有者へCancel Eventを配送してからCapture / Pressedを解除します。
    // Mouse Upが届かない異常終了経路では、単にPointerをnullptrへするのではなく必ずこちらを利用します。
    void CancelMouseCapture();

    // 後方互換の強制Release入口です。所有者を指定しない解除はDragの強制終了とみなしCancelへ委譲します。
    void ReleaseMouseCapture();
    bool HasMouseCapture() const;
    bool HasMouseCapture(const UIElement* element) const;
    UIElement* GetMouseCaptureElement();
    const UIElement* GetMouseCaptureElement() const;

    // Downを受けたWidgetが呼び出します。閾値を超えるまで通常Clickを維持します。
    // Payloadは値で保持し、Drag中に呼び出し元の一時データが破棄されても安全です。
    bool BeginDrag(UIElement* source, UIDragDropPayload payload, const math::Vec2& startPosition);
    void CancelDrag();
    // Pointerが静止していてもDrag先を更新します。テストから時間を指定可能です。
    void TickDrag(float deltaSeconds);
    // Drag元が任意で設定するPreview文字列。Font未設定時は従来の矩形表示です。
    void SetDragPreview(std::string text, const Ref<UIFontAtlas>& font);
    const std::string& GetDragPreviewText() const { return m_DragPreviewText; }
    bool IsDragging() const { return m_DragActive; }
    bool HasPendingDrag() const { return m_DragSource != nullptr; }
    UIElement* GetDragSource() const { return m_DragSource; }
    UIElement* GetDropTarget() const { return m_DropTarget; }
    void SetDragThreshold(float pixels) { m_DragThreshold = std::max(0.0f, pixels); }

    // PopupはRoot末尾の専用Layerへ所有させ、通常のPanel Clipから分離します。
    UIElement* AddPopup(Scope<UIElement> popup);
    bool RemovePopup(UIElement* popup);
    bool OpenPopup(UIElement* popup);
    // AnchorのScreen位置からPopupの配置を決定し、Viewport外へはみ出す場合は反対側へ展開します。
    bool OpenPopupAt(UIElement* popup, const UIElement* anchor, float gap = 4.0f);
    void ClosePopup();
    UIElement* GetOpenPopup() const { return m_OpenPopup; }

    // TooltipはPopupと独立した描画専用Overlayです。targetのTree離脱時は自動解除します。
    bool SetTooltip(UIElement* target, std::string text,
        const Ref<UIFontAtlas>& font, float delaySeconds = 0.5f);
    bool ClearTooltip(UIElement* target);
    const UITooltip* GetVisibleTooltip() const;
    void UpdateTooltip();

    // ContextごとにThemeを値保持します。Widgetは描画時に参照するためTree再構築は不要です。
    void SetTheme(const UITheme& theme) { m_Theme = theme; }
    const UITheme& GetTheme() const { return m_Theme; }

    void SetRenderer(Scope<UIRenderer> renderer);

    // Root直下の通常Widgetを別UIContextへ移譲します。Popup/Tooltipなどの内部Layerは対象外です。
    // DetachChildが旧ContextのCapture/Focus/IMEを解除し、AddChildが新DPIを適用します。
    // 描画中のTree変更を避けるため、両ContextのFrame外で呼び出してください。
    bool TransferRootChildTo(UIContext& destination, UIElement* child);

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

    bool IsLiveDragElement(const UIElement* element) const;
    void UpdateDrag(const math::Vec2& position, UIElement* hitTarget, float deltaSeconds = 0.0f);
    void FinishDrag(const math::Vec2& position, UIElement* hitTarget);
    void SendDragEvent(UIElement* element, UIDragDropEventType type, const math::Vec2& position);
    void HideTooltip();
    void UpdateHoverTarget(UIElement* target);
    void UpdatePressedTarget(UIElement* target);
    void CollectFocusableElements(UIElement* root, std::vector<UIElement*>& outElements) const;

    // Retained TreeからSubtreeを破棄する直前にUIElementから呼ばれます。
    // Capture / Hover / Pressedが破棄対象を指したままScopeが解放されるとdangling pointerになるため、
    // Elementがまだ生存しParent chainも有効な段階でInteraction Stateを安全に終了します。
    void OnSubtreeRemoving(UIElement* subtreeRoot);
    static bool IsElementInSubtree(const UIElement* element, const UIElement* subtreeRoot);

private:
    UITheme m_Theme = UITheme::CreateDefaultDark();
    math::Vec2 m_FramebufferSize = math::Vec2(0.0f, 0.0f);
    bool m_HasFramebufferSize = false;
    float m_DPIScaleX = 1.0f;
    float m_DPIScaleY = 1.0f;
    float m_UserScale = 1.0f;
    math::Vec2 m_ViewportSize{};
    UIDrawList m_DrawList;
    Scope<UIElement> m_RootElement;
    UIElement* m_PopupLayer = nullptr;
    UITooltip* m_Tooltip = nullptr;
    struct TooltipRegistration
    {
        UIElement* Target = nullptr;
        std::string Text;
        Ref<UIFontAtlas> Font;
        float DelaySeconds = 0.5f;
    };
    std::vector<TooltipRegistration> m_Tooltips;
    UIElement* m_TooltipTarget = nullptr;
    std::chrono::steady_clock::time_point m_HoverStarted{};
    math::Vec2 m_LastPointerPosition{};
    bool m_TooltipSuppressedUntilMove = false;
    UIElement* m_OpenPopup = nullptr;
    Scope<UIRenderer> m_Renderer;
    UIElement* m_HoveredElement = nullptr;
    UIElement* m_PressedElement = nullptr;
    UIElement* m_MouseCaptureElement = nullptr;
    UIElement* m_DragSource = nullptr;
    UIElement* m_DropTarget = nullptr;
    UIDragDropPayload m_DragPayload;
    math::Vec2 m_DragStart{};
    float m_DragThreshold = 5.0f;
    bool m_DragActive = false;
    std::chrono::steady_clock::time_point m_LastDragTick{};
    std::string m_DragPreviewText;
    Ref<UIFontAtlas> m_DragPreviewFont;
    bool m_FrameActive = false;
    std::function<void()> m_IMECancelCallback;
};

} // namespace Raven

#include "Raven/UI/Core/UIFocusNavigation.inl"
