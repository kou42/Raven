#include "Raven/Editor/Panels/EditorViewportRavenPanel.h"

#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/Renderer/RHI/RHITexture.h"
#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Widgets/UIImage.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIWindow.h"

#include <algorithm>
#include <utility>

namespace Raven
{

namespace
{

bool IsGizmoSnapEnabled()
{
    return Input::IsKeyPressed(Key::LeftControl) == true ||
        Input::IsKeyPressed(Key::RightControl) == true;
}

void AddThickLine(
    UIDrawList& drawList,
    const math::Vec2& absolutePosition,
    const EditorGizmoPrimitive& primitive)
{
    const math::Vec2 segment = primitive.End - primitive.Start;
    const float length = segment.Length();
    if (length <= math::Epsilon || primitive.Thickness <= 0.0f)
    {
        return;
    }
    const float halfThickness = primitive.Thickness * 0.5f;
    const math::Vec2 normal(
        -segment.y / length * halfThickness,
        segment.x / length * halfThickness);
    drawList.AddPolygon(
        {
            absolutePosition + primitive.Start + normal,
            absolutePosition + primitive.End + normal,
            absolutePosition + primitive.End - normal,
            absolutePosition + primitive.Start - normal
        },
        primitive.Color);
}

class EditorGizmoOverlay final : public UIElement
{
public:
    void SetDrawData(const EditorGizmoDrawData& drawData)
    {
        m_DrawData = drawData;
    }

protected:
    void OnBuildDrawList(
        UIDrawList& drawList,
        const math::Vec2& absolutePosition) const override
    {
        for (const EditorGizmoPrimitive& primitive : m_DrawData.Primitives)
        {
            if (primitive.Type == EditorGizmoPrimitiveType::Line)
            {
                AddThickLine(drawList, absolutePosition, primitive);
            }
            else if (primitive.Type == EditorGizmoPrimitiveType::Circle)
            {
                const math::Vec2 radius(primitive.Radius, primitive.Radius);
                drawList.AddCircle(
                    absolutePosition + primitive.Start - radius,
                    absolutePosition + primitive.Start + radius,
                    primitive.Color);
            }
            else if (primitive.Type == EditorGizmoPrimitiveType::Square)
            {
                const math::Vec2 radius(primitive.Radius, primitive.Radius);
                drawList.AddRect(
                    absolutePosition + primitive.Start - radius,
                    absolutePosition + primitive.Start + radius,
                    primitive.Color);
            }
        }
    }

private:
    EditorGizmoDrawData m_DrawData;
};

} // namespace

// Image描画と入力所有権を同じ矩形へ集約するViewport専用Regionです。
// UIImage自体は汎用表示Widgetのまま維持し、Editor PickingやMouse Captureを持ち込みません。
class EditorViewportInputRegion final : public UIElement
{
public:
    using PickingRequestedHandler =
        EditorViewportRavenPanel::PickingRequestedHandler;
    using GizmoPointerHandler =
        EditorViewportRavenPanel::GizmoPointerHandler;

    EditorViewportInputRegion()
    {
        SetFocusable(true);
        SetClipChildren(true);

        auto image = CreateScope<UIImage>();
        image->SetScaleMode(UIImageScaleMode::AspectFit);
        // Hit TestはRegionが一元的に処理し、Imageと親で入力所有権が分裂しないようにします。
        image->SetHitTestVisible(false);
        m_Image = static_cast<UIImage*>(AddChild(std::move(image)));

        auto gizmoOverlay = CreateScope<EditorGizmoOverlay>();
        gizmoOverlay->SetHitTestVisible(false);
        m_GizmoOverlay = static_cast<EditorGizmoOverlay*>(
            AddChild(std::move(gizmoOverlay)));
    }

    void SetViewportSize(const math::Vec2& size)
    {
        if (GetSize() != size)
        {
            SetSize(size);
        }
        if (m_Image != nullptr && m_Image->GetSize() != size)
        {
            m_Image->SetSize(size);
        }
        if (m_GizmoOverlay != nullptr && m_GizmoOverlay->GetSize() != size)
        {
            m_GizmoOverlay->SetSize(size);
        }
    }

    void SetRenderTargetTexture(const Ref<Texture>& texture)
    {
        if (m_Image != nullptr)
        {
            m_Image->SetRenderTargetTexture(
                texture, UITextureOrigin::BottomLeft);
        }
    }

    void SetRHIRenderTargetTexture(const Ref<RHITexture>& texture)
    {
        if (m_Image != nullptr)
        {
            // DX12/VulkanのViewport規約は左上原点へ統一し、OpenGLだけをBottomLeftとして扱います。
            m_Image->SetRHIRenderTargetTexture(
                texture, UITextureOrigin::TopLeft);
        }
    }

    void SetPickingRequestedHandler(PickingRequestedHandler handler)
    {
        m_PickingRequestedHandler = std::move(handler);
    }

    void SetGizmoPointerHandler(GizmoPointerHandler handler)
    {
        m_GizmoPointerHandler = std::move(handler);
    }

    void SetGizmoDrawData(const EditorGizmoDrawData& drawData)
    {
        if (m_GizmoOverlay != nullptr)
        {
            m_GizmoOverlay->SetDrawData(drawData);
        }
    }

    bool TryGetGizmoViewportRect(EditorGizmoViewportRect& outRect) const
    {
        if (m_Image == nullptr)
        {
            return false;
        }
        const UIRect imageRect = m_Image->GetImageRect();
        if (imageRect.Max.x <= imageRect.Min.x ||
            imageRect.Max.y <= imageRect.Min.y)
        {
            return false;
        }
        outRect.Min = imageRect.Min;
        outRect.Max = imageRect.Max;
        return true;
    }

    bool TryMapScreenPositionToViewportLocal(
        const math::Vec2& screenPosition,
        math::Vec2& outPosition) const
    {
        return TryScreenToLocalPosition(screenPosition, outPosition);
    }

    bool TryMapScreenPositionToTexturePixel(
        const math::Vec2& screenPosition,
        std::uint32_t& outX,
        std::uint32_t& outY) const
    {
        return m_Image != nullptr &&
            m_Image->TryMapScreenPositionToTexturePixel(
                screenPosition, outX, outY) == true;
    }

protected:
    void OnMouseEvent(UIMouseEvent& event) override
    {
        // Viewport Panel上のMouse入力は映像外Letterboxを含めてEditor UIが所有します。
        // 実映像外ClickはPickingしませんが、背後のRuntime Sceneへも伝播させません。
        event.Handled = true;
        if (event.Type == UIMouseEventType::Cancel)
        {
            if (m_GizmoPointerHandler != nullptr)
            {
                EditorGizmoPointerEvent gizmoEvent;
                gizmoEvent.Type = EditorGizmoPointerEventType::Cancel;
                m_GizmoPointerHandler(gizmoEvent);
            }
            return;
        }

        math::Vec2 localPosition{};
        EditorGizmoViewportRect gizmoViewport{};
        const bool hasGizmoCoordinates =
            TryMapScreenPositionToViewportLocal(
                event.ScreenPosition, localPosition) == true &&
            TryGetGizmoViewportRect(gizmoViewport) == true;

        if ((event.Type == UIMouseEventType::Move ||
                event.Button == UIMouseButton::Left) &&
            m_GizmoPointerHandler != nullptr &&
            hasGizmoCoordinates == true)
        {
            EditorGizmoPointerEvent gizmoEvent;
            gizmoEvent.Type = event.Type == UIMouseEventType::Down
                ? EditorGizmoPointerEventType::Down
                : event.Type == UIMouseEventType::Up
                    ? EditorGizmoPointerEventType::Up
                    : EditorGizmoPointerEventType::Move;
            gizmoEvent.Position = localPosition;
            gizmoEvent.Viewport = gizmoViewport;
            gizmoEvent.Snap = IsGizmoSnapEnabled();
            if (m_GizmoPointerHandler(gizmoEvent) == true)
            {
                if (event.Type == UIMouseEventType::Down && event.Context != nullptr)
                {
                    event.Context->SetFocus(this);
                    event.Context->CaptureMouse(this);
                }
                if (event.Type == UIMouseEventType::Up && event.Context != nullptr &&
                    event.Context->HasMouseCapture(this) == true)
                {
                    event.Context->ReleaseMouseCapture(this);
                }
                return;
            }
        }

        if (event.Type == UIMouseEventType::Down)
        {
            std::uint32_t pixelX = 0u;
            std::uint32_t pixelY = 0u;
            const bool overImage = TryMapScreenPositionToTexturePixel(
                event.ScreenPosition, pixelX, pixelY);
            if (overImage == false)
            {
                if (event.Context != nullptr && IsFocused() == true)
                {
                    event.Context->ClearFocus();
                }
                return;
            }

            if (event.Context != nullptr)
            {
                event.Context->SetFocus(this);
                // Downを受けたViewportがUpまで所有し、Pointerが外へ出た場合も
                // Runtimeへ片側だけのButton Eventを渡さないようにします。
                event.Context->CaptureMouse(this);
            }
            if (event.Button == UIMouseButton::Left &&
                m_PickingRequestedHandler != nullptr)
            {
                m_PickingRequestedHandler(pixelX, pixelY);
            }
            return;
        }

        if (event.Type == UIMouseEventType::Up && event.Context != nullptr &&
            event.Context->HasMouseCapture(this) == true)
        {
            event.Context->ReleaseMouseCapture(this);
        }
    }

private:
    UIImage* m_Image = nullptr;
    EditorGizmoOverlay* m_GizmoOverlay = nullptr;
    PickingRequestedHandler m_PickingRequestedHandler;
    GizmoPointerHandler m_GizmoPointerHandler;
};

bool EditorViewportRavenPanel::Attach(
    UIContext& context,
    const Ref<UIFontAtlas>& font,
    const std::string& title,
    const math::Vec2& position,
    const math::Vec2& size)
{
    if (m_Root != nullptr)
    {
        return false;
    }

    auto window = CreateScope<UIWindow>();
    window->SetTitle(title);
    window->SetPosition(position);
    window->SetSize(size);
    window->SetPreferredSize(size);

    auto titleLabel = CreateScope<UILabel>();
    titleLabel->SetFont(font);
    titleLabel->SetText(title);
    titleLabel->SetPosition(math::Vec2(10.0f, 3.0f));
    titleLabel->SetSize(math::Vec2(std::max(0.0f, size.x - 20.0f), 22.0f));
    titleLabel->SetHitTestVisible(false);
    window->AddChild(std::move(titleLabel));

    auto inputRegion = CreateScope<EditorViewportInputRegion>();
    inputRegion->SetPosition(math::Vec2(0.0f, TitleBarHeight));
    inputRegion->SetViewportSize(math::Vec2(
        std::max(0.0f, size.x),
        std::max(0.0f, size.y - TitleBarHeight)));
    inputRegion->SetPickingRequestedHandler(m_PickingRequestedHandler);
    inputRegion->SetGizmoPointerHandler(m_GizmoPointerHandler);
    m_InputRegion = static_cast<EditorViewportInputRegion*>(
        window->AddChild(std::move(inputRegion)));

    m_Context = &context;
    m_Root = context.AddRootChild(std::move(window));
    if (m_Root == nullptr)
    {
        m_Context = nullptr;
        m_InputRegion = nullptr;
        return false;
    }
    return true;
}

void EditorViewportRavenPanel::Detach()
{
    if (m_Root != nullptr && m_Root->GetParent() != nullptr)
    {
        m_Root->GetParent()->RemoveChild(m_Root);
    }
    m_Context = nullptr;
    m_Root = nullptr;
    m_InputRegion = nullptr;
}

void EditorViewportRavenPanel::Update(const Ref<Texture>& renderTargetTexture)
{
    if (m_Root == nullptr || m_InputRegion == nullptr)
    {
        return;
    }

    // UIWindow ResizeはWindow本体のSizeだけを更新するため、Contentを毎Frame追従させます。
    // Textureは前のOnRender()で確定したAttachmentです。Resizeによる交換後もRefが旧Resourceを
    // 描画完了まで保持し、次FrameのUpdateで新Attachmentへ安全に差し替えます。
    const math::Vec2 windowSize = m_Root->GetSize();
    const math::Vec2 imageSize(
        std::max(0.0f, windowSize.x),
        std::max(0.0f, windowSize.y - TitleBarHeight));
    m_InputRegion->SetViewportSize(imageSize);
    m_InputRegion->SetRenderTargetTexture(renderTargetTexture);
}

void EditorViewportRavenPanel::Update(const Ref<RHITexture>& renderTargetTexture)
{
    if (m_Root == nullptr || m_InputRegion == nullptr)
    {
        return;
    }

    // UIDrawList内のUITextureViewがRefを保持します。Explicit BackendがResizeでColor Attachmentを
    // 交換しても、既に準備済みのUI Frameが古いGPU Resourceを参照中に破棄されません。
    const math::Vec2 windowSize = m_Root->GetSize();
    const math::Vec2 imageSize(
        std::max(0.0f, windowSize.x),
        std::max(0.0f, windowSize.y - TitleBarHeight));
    m_InputRegion->SetViewportSize(imageSize);
    m_InputRegion->SetRHIRenderTargetTexture(renderTargetTexture);
}

void EditorViewportRavenPanel::SetVisible(bool visible)
{
    if (m_Root != nullptr)
    {
        m_Root->SetVisible(visible);
    }
}

void EditorViewportRavenPanel::SetPickingRequestedHandler(
    PickingRequestedHandler handler)
{
    m_PickingRequestedHandler = std::move(handler);
    if (m_InputRegion != nullptr)
    {
        m_InputRegion->SetPickingRequestedHandler(m_PickingRequestedHandler);
    }
}

void EditorViewportRavenPanel::SetGizmoPointerHandler(
    GizmoPointerHandler handler)
{
    m_GizmoPointerHandler = std::move(handler);
    if (m_InputRegion != nullptr)
    {
        m_InputRegion->SetGizmoPointerHandler(m_GizmoPointerHandler);
    }
}

void EditorViewportRavenPanel::SetGizmoDrawData(
    const EditorGizmoDrawData& drawData)
{
    if (m_InputRegion != nullptr)
    {
        m_InputRegion->SetGizmoDrawData(drawData);
    }
}

bool EditorViewportRavenPanel::IsAttached() const
{
    return m_Root != nullptr;
}

bool EditorViewportRavenPanel::IsVisible() const
{
    return m_Root != nullptr && m_Root->IsVisible() == true;
}

math::Vec2 EditorViewportRavenPanel::GetViewportLogicalSize() const
{
    return m_InputRegion != nullptr ? m_InputRegion->GetSize() : math::Vec2{};
}

bool EditorViewportRavenPanel::IsPointerOverViewport(
    const math::Vec2& screenPosition) const
{
    if (m_Context == nullptr || m_InputRegion == nullptr || IsVisible() == false)
    {
        return false;
    }

    const UIInteractionState state =
        m_Context->GetInteractionState(m_InputRegion);
    std::uint32_t pixelX = 0u;
    std::uint32_t pixelY = 0u;
    return state.Hovered == true &&
        m_InputRegion->TryMapScreenPositionToTexturePixel(
            screenPosition, pixelX, pixelY) == true;
}

bool EditorViewportRavenPanel::TryGetGizmoViewportRect(
    EditorGizmoViewportRect& outRect) const
{
    return m_InputRegion != nullptr &&
        m_InputRegion->TryGetGizmoViewportRect(outRect) == true;
}

bool EditorViewportRavenPanel::TryMapScreenPositionToViewportLocal(
    const math::Vec2& screenPosition,
    math::Vec2& outPosition) const
{
    return m_InputRegion != nullptr &&
        m_InputRegion->TryMapScreenPositionToViewportLocal(
            screenPosition, outPosition) == true;
}

bool EditorViewportRavenPanel::HasKeyboardFocus() const
{
    if (m_Context == nullptr || m_InputRegion == nullptr || IsVisible() == false)
    {
        return false;
    }
    return m_Context->GetInteractionState(m_InputRegion).Focused;
}

} // namespace Raven
