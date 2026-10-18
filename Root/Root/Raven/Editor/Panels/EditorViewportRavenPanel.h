#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Editor/EditorGizmo.h"
#include "Raven/Math/MathVector.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Raven
{

class Texture;
class RHITexture;
class UIContext;
class UIElement;
class UIFontAtlas;
class EditorViewportInputRegion;

// Scene View / Game ViewのFramebuffer Textureを表示するRaven UI Panelです。
// Framebuffer自体は所有せず、EditorLayerが毎Frame渡すColor AttachmentだけをRefで保持します。
class EditorViewportRavenPanel
{
public:
    using PickingRequestedHandler =
        std::function<void(std::uint32_t, std::uint32_t)>;
    using GizmoPointerHandler =
        std::function<bool(const EditorGizmoPointerEvent&)>;

    bool Attach(
        UIContext& context,
        const Ref<UIFontAtlas>& font,
        const std::string& title,
        const math::Vec2& position,
        const math::Vec2& size);
    void Detach();
    void Update(const Ref<Texture>& renderTargetTexture);
    // Explicit BackendのColor Attachmentはnative handleへ変換せずRHITextureのRefを渡します。
    void Update(const Ref<RHITexture>& renderTargetTexture);
    void SetVisible(bool visible);
    void SetPickingRequestedHandler(PickingRequestedHandler handler);
    void SetGizmoPointerHandler(GizmoPointerHandler handler);
    void SetGizmoDrawData(const EditorGizmoDrawData& drawData);

    bool IsAttached() const;
    bool IsVisible() const;
    math::Vec2 GetViewportLogicalSize() const;
    bool IsPointerOverViewport(const math::Vec2& screenPosition) const;
    bool TryGetGizmoViewportRect(EditorGizmoViewportRect& outRect) const;
    bool TryMapScreenPositionToViewportLocal(
        const math::Vec2& screenPosition,
        math::Vec2& outPosition) const;
    bool HasKeyboardFocus() const;

private:
    static constexpr float TitleBarHeight = 28.0f;

    UIContext* m_Context = nullptr;
    UIElement* m_Root = nullptr;
    EditorViewportInputRegion* m_InputRegion = nullptr;
    PickingRequestedHandler m_PickingRequestedHandler;
    GizmoPointerHandler m_GizmoPointerHandler;
};

} // namespace Raven
