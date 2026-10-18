#include "Raven/Editor/Tests/EditorViewportGeometrySelfTests.h"

#include "Raven/Editor/EditorViewportGeometry.h"
#include "Raven/Editor/EditorCamera.h"
#include "Raven/Editor/EditorCommandHistory.h"
#include "Raven/Editor/Panels/EditorViewportRavenPanel.h"
#include "Raven/Renderer/RHI/RHIOffscreenRenderTarget.h"
#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"
#include "Raven/UI/Core/UIContext.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace Raven::tests
{
namespace
{

class EditorViewportFixtureTexture final : public Texture
{
public:
    EditorViewportFixtureTexture(std::uint32_t width, std::uint32_t height)
    {
        m_Specification.Width = width;
        m_Specification.Height = height;
        m_Specification.Usage = TextureUsage::RenderTarget;
        m_Specification.GenerateMips = false;
    }

    void Bind(unsigned int slot = 0u) const override { static_cast<void>(slot); }
    void Unbind() const override {}
    void SetData(const void* data, std::size_t dataSize) override
    {
        static_cast<void>(data);
        static_cast<void>(dataSize);
    }
    unsigned int GetID() const override { return 1u; }
    int GetWidth() const override { return static_cast<int>(m_Specification.Width); }
    int GetHeight() const override { return static_cast<int>(m_Specification.Height); }
    const TextureSpecification& GetSpecification() const override
    {
        return m_Specification;
    }

private:
    TextureSpecification m_Specification{};
};

class EditorViewportFixtureRHITexture final : public RHITexture
{
public:
    EditorViewportFixtureRHITexture(std::uint32_t width, std::uint32_t height)
    {
        m_Specification.Width = width;
        m_Specification.Height = height;
        m_Specification.Format = RHITextureFormat::RGBA8;
        m_Specification.Usage = RHITextureUsage::RenderTarget;
        m_Specification.GenerateMips = false;
    }

    void SetData(const void* data, std::size_t dataSize) override
    {
        static_cast<void>(data);
        static_cast<void>(dataSize);
    }

    const RHITextureSpecification& GetSpecification() const override
    {
        return m_Specification;
    }

private:
    RHITextureSpecification m_Specification{};
};

void RunEditorViewportInputRegionSelfTests()
{
    UIContext context;
    EditorViewportRavenPanel panel;
    std::uint32_t pickedX = 0u;
    std::uint32_t pickedY = 0u;
    std::uint32_t pickingRequestCount = 0u;
    panel.SetPickingRequestedHandler(
        [&](std::uint32_t pixelX, std::uint32_t pixelY)
        {
            pickedX = pixelX;
            pickedY = pixelY;
            ++pickingRequestCount;
        });

    assert(panel.Attach(
        context,
        Ref<UIFontAtlas>{},
        "Scene View",
        math::Vec2(10.0f, 10.0f),
        math::Vec2(220.0f, 228.0f)) == true);
    const Ref<Texture> texture = CreateRef<EditorViewportFixtureTexture>(200u, 100u);
    panel.Update(texture);
    context.BeginFrame(math::Vec2(800.0f, 600.0f));
    context.EndFrame();

    // Contentは220x200、Textureは2:1なので実映像はY=83..193にAspectFitされます。
    // 上側LetterboxのClickはUIが消費しますがPickingやFocusには変換しません。
    assert(context.RouteMouseDown(
        math::Vec2(20.0f, 50.0f), UIMouseButton::Left) == true);
    assert(pickingRequestCount == 0u);
    assert(panel.HasKeyboardFocus() == false);
    assert(context.RouteMouseUp(
        math::Vec2(20.0f, 50.0f), UIMouseButton::Left) == true);

    // 実映像の左上はBottomLeft原点Attachmentの(0, 99)へ変換されます。
    assert(context.RouteMouseDown(
        math::Vec2(10.0f, 83.0f), UIMouseButton::Left) == true);
    assert(pickingRequestCount == 1u);
    assert(pickedX == 0u);
    assert(pickedY == 99u);
    assert(panel.HasKeyboardFocus() == true);
    assert(panel.IsPointerOverViewport(math::Vec2(10.0f, 83.0f)) == true);
    assert(context.HasMouseCapture() == true);

    // Capture中でもHoverは物理位置を追跡し、映像外ではCamera有効判定をfalseにします。
    assert(context.RouteMouseMove(math::Vec2(500.0f, 500.0f)) == true);
    assert(panel.IsPointerOverViewport(math::Vec2(500.0f, 500.0f)) == false);
    assert(context.RouteMouseUp(
        math::Vec2(500.0f, 500.0f), UIMouseButton::Left) == true);
    assert(context.HasMouseCapture() == false);

    std::uint32_t gizmoEventCount = 0u;
    bool gizmoActive = false;
    panel.SetGizmoPointerHandler(
        [&](const EditorGizmoPointerEvent& event)
        {
            ++gizmoEventCount;
            if (event.Type == EditorGizmoPointerEventType::Down)
            {
                gizmoActive = true;
                return true;
            }
            if (event.Type == EditorGizmoPointerEventType::Up ||
                event.Type == EditorGizmoPointerEventType::Cancel)
            {
                const bool consumed = gizmoActive;
                gizmoActive = false;
                return consumed;
            }
            return gizmoActive;
        });

    // GizmoがDownを取得した場合は同じClickをPickingへ渡さず、Capture中のMove/Upを一貫して所有します。
    assert(context.RouteMouseDown(
        math::Vec2(20.0f, 100.0f), UIMouseButton::Left) == true);
    assert(gizmoEventCount == 1u);
    assert(pickingRequestCount == 1u);
    assert(context.HasMouseCapture() == true);
    assert(context.RouteMouseMove(math::Vec2(500.0f, 500.0f)) == true);
    assert(context.RouteMouseUp(
        math::Vec2(500.0f, 500.0f), UIMouseButton::Left) == true);
    assert(gizmoEventCount == 3u);
    assert(context.HasMouseCapture() == false);

    assert(context.RouteMouseDown(
        math::Vec2(20.0f, 100.0f), UIMouseButton::Left) == true);
    assert(context.HasMouseCapture() == true);
    context.CancelMouseCapture();
    assert(gizmoEventCount == 5u);
    assert(gizmoActive == false);
    assert(context.HasMouseCapture() == false);

    // Explicit BackendのColor AttachmentはTopLeft原点のRHITextureとして同じPanelへ渡します。
    // DrawListがRefを保持し、OpenGL用Legacy Textureへ変換されていないことも確認します。
    panel.SetGizmoPointerHandler({});
    const Ref<RHITexture> rhiTexture =
        CreateRef<EditorViewportFixtureRHITexture>(200u, 100u);
    panel.Update(rhiTexture);
    context.BeginFrame(math::Vec2(800.0f, 600.0f));
    context.EndFrame();
    bool foundRHIImage = false;
    for (const UIDrawCommand& command : context.GetDrawList().GetCommands())
    {
        if (command.Type == UIDrawCommandType::Image &&
            command.TextureView.GetRHITexture() == rhiTexture)
        {
            foundRHIImage = true;
            assert(command.TextureView.GetSourceType() ==
                UITextureSourceType::RHITexture);
            assert(command.TextureView.GetOrigin() == UITextureOrigin::TopLeft);
        }
    }
    assert(foundRHIImage == true);

    const std::uint32_t previousPickingCount = pickingRequestCount;
    assert(context.RouteMouseDown(
        math::Vec2(10.0f, 83.0f), UIMouseButton::Left) == true);
    assert(pickingRequestCount == previousPickingCount + 1u);
    assert(pickedX == 0u);
    assert(pickedY == 0u);
    assert(context.RouteMouseUp(
        math::Vec2(10.0f, 83.0f), UIMouseButton::Left) == true);

    panel.Detach();
}

void RunEditorGizmoGeometrySelfTests()
{
    const EditorGizmoViewportRect viewport{
        math::Vec2(100.0f, 50.0f),
        math::Vec2(300.0f, 250.0f)
    };
    math::Vec2 projected{};
    assert(ProjectEditorGizmoWorldToViewport(
        math::Vec3(0.0f, 0.0f, 0.0f),
        math::Mat4::Identity(),
        viewport,
        projected) == true);
    assert(std::fabs(projected.x - 200.0f) < 0.0001f);
    assert(std::fabs(projected.y - 150.0f) < 0.0001f);

    assert(std::fabs(DistancePointToEditorGizmoSegment(
        math::Vec2(5.0f, 3.0f),
        math::Vec2(0.0f, 0.0f),
        math::Vec2(10.0f, 0.0f)) - 3.0f) < 0.0001f);

    EditorCamera rayCamera;
    rayCamera.SetViewportSize(400.0f, 200.0f);
    math::Vec3 rayOrigin{};
    math::Vec3 rayDirection{};
    assert(rayCamera.TryBuildWorldRay(
        0.5f, 0.5f, rayOrigin, rayDirection) == true);
    assert((rayOrigin - rayCamera.GetPosition()).LengthSq() < 0.000001f);
    assert(math::Vec3::Dot(
        rayDirection, rayCamera.GetForwardDirection()) > 0.9999f);

    math::Vec3 planeNormal{};
    assert(TryBuildEditorGizmoDragPlaneNormal(
        math::Vec3(1.0f, 0.0f, 0.0f),
        math::Vec3(0.0f, 0.0f, -1.0f),
        planeNormal) == true);
    assert(std::fabs(math::Vec3::Dot(
        planeNormal, math::Vec3(1.0f, 0.0f, 0.0f))) < 0.0001f);

    // Camera視線と操作軸が平行な場合はPlaneを無理に作らずScreen軸fallbackを選びます。
    assert(TryBuildEditorGizmoDragPlaneNormal(
        math::Vec3(1.0f, 0.0f, 0.0f),
        math::Vec3(1.0f, 0.0f, 0.0f),
        planeNormal) == false);

    math::Vec3 intersection{};
    assert(TryIntersectEditorGizmoRayPlane(
        math::Vec3(0.0f, 0.0f, 5.0f),
        math::Vec3(0.0f, 0.0f, -1.0f),
        math::Vec3(0.0f, 0.0f, 0.0f),
        math::Vec3(0.0f, 0.0f, 1.0f),
        intersection) == true);
    assert(intersection.LengthSq() < 0.000001f);
    assert(TryIntersectEditorGizmoRayPlane(
        math::Vec3(0.0f, 0.0f, 5.0f),
        math::Vec3(1.0f, 0.0f, 0.0f),
        math::Vec3(0.0f, 0.0f, 0.0f),
        math::Vec3(0.0f, 0.0f, 1.0f),
        intersection) == false);

    const math::Vec3 rotated = ApplyEditorGizmoRotationDelta(
        math::Vec3{},
        EditorGizmoAxis::Z,
        EditorGizmoSpace::World,
        1.57079632679f);
    assert(std::fabs(rotated.z - 1.57079632679f) < 0.0001f);

    assert(ApplyEditorGizmoMinimumScale(-2.0f, 1.0f) == 0.01f);
    assert(ApplyEditorGizmoMinimumScale(2.0f, -1.0f) == -0.01f);
    assert(std::fabs(ApplyEditorGizmoSnap(0.74f, 0.5f) - 0.5f) < 0.0001f);
}

void RunEditorGizmoControllerSelfTests()
{
    Scene scene;
    Entity entity = scene.CreateEntity("Gizmo Target");
    SetEditorCommandHistoryScene(&scene);

    EditorCamera camera;
    camera.SetViewportSize(400.0f, 400.0f);
    const EditorGizmoViewportRect viewport{
        math::Vec2(0.0f, 0.0f),
        math::Vec2(400.0f, 400.0f)
    };
    EditorTransformGizmoController controller;

    SetEditorGizmoOperation(EditorGizmoOperation::Translate);
    const EditorGizmoDrawData translateDraw = controller.BuildDrawData(
        entity, camera, viewport, math::Vec2(-100.0f, -100.0f));
    assert(translateDraw.Primitives.empty() == false);
    const EditorGizmoPrimitive translateAxis = translateDraw.Primitives.front();
    const math::Vec2 translateDirection =
        (translateAxis.End - translateAxis.Start).Normalized();
    EditorGizmoPointerEvent pointerEvent;
    pointerEvent.Type = EditorGizmoPointerEventType::Down;
    pointerEvent.Position = translateAxis.End;
    pointerEvent.Viewport = viewport;
    assert(controller.HandlePointerEvent(entity, camera, pointerEvent) == true);
    pointerEvent.Type = EditorGizmoPointerEventType::Up;
    pointerEvent.Position = translateAxis.End + translateDirection * 30.0f;
    assert(controller.HandlePointerEvent(entity, camera, pointerEvent) == true);
    assert(entity.GetComponent<TransformComponent>().Position.LengthSq() > 0.0f);
    assert(CanUndoEditorCommand() == true);
    assert(UndoEditorCommand() == true);
    assert(entity.GetComponent<TransformComponent>().Position.LengthSq() < 0.000001f);

    ClearEditorCommandHistory();
    SetEditorCommandHistoryScene(&scene);
    SetEditorGizmoOperation(EditorGizmoOperation::Scale);
    const EditorGizmoDrawData scaleDraw = controller.BuildDrawData(
        entity, camera, viewport, math::Vec2(-100.0f, -100.0f));
    assert(scaleDraw.Primitives.empty() == false);
    const EditorGizmoPrimitive scaleAxis = scaleDraw.Primitives.front();
    const math::Vec2 scaleDirection =
        (scaleAxis.End - scaleAxis.Start).Normalized();
    pointerEvent.Type = EditorGizmoPointerEventType::Down;
    pointerEvent.Position = scaleAxis.End;
    assert(controller.HandlePointerEvent(entity, camera, pointerEvent) == true);
    pointerEvent.Type = EditorGizmoPointerEventType::Up;
    pointerEvent.Position = scaleAxis.End + scaleDirection * 30.0f;
    assert(controller.HandlePointerEvent(entity, camera, pointerEvent) == true);
    assert(entity.GetComponent<TransformComponent>().Scale != math::Vec3(1.0f, 1.0f, 1.0f));
    assert(UndoEditorCommand() == true);
    assert(entity.GetComponent<TransformComponent>().Scale == math::Vec3(1.0f, 1.0f, 1.0f));

    ClearEditorCommandHistory();
    SetEditorCommandHistoryScene(&scene);
    SetEditorGizmoOperation(EditorGizmoOperation::Rotate);
    const EditorGizmoDrawData rotateDraw = controller.BuildDrawData(
        entity, camera, viewport, math::Vec2(-100.0f, -100.0f));
    assert(rotateDraw.Primitives.empty() == false);
    const EditorGizmoPrimitive ringSegment = rotateDraw.Primitives.front();
    const math::Vec2 ringPosition =
        (ringSegment.Start + ringSegment.End) * 0.5f;
    const math::Vec2 center(200.0f, 200.0f);
    const math::Vec2 radial = ringPosition - center;
    constexpr float rotationDelta = 0.35f;
    const math::Vec2 rotatedPosition(
        center.x + std::cos(rotationDelta) * radial.x -
            std::sin(rotationDelta) * radial.y,
        center.y + std::sin(rotationDelta) * radial.x +
            std::cos(rotationDelta) * radial.y);
    pointerEvent.Type = EditorGizmoPointerEventType::Down;
    pointerEvent.Position = ringPosition;
    assert(controller.HandlePointerEvent(entity, camera, pointerEvent) == true);
    pointerEvent.Type = EditorGizmoPointerEventType::Up;
    pointerEvent.Position = rotatedPosition;
    assert(controller.HandlePointerEvent(entity, camera, pointerEvent) == true);
    assert(entity.GetComponent<TransformComponent>().Rotation.LengthSq() > 0.000001f);
    assert(UndoEditorCommand() == true);
    assert(entity.GetComponent<TransformComponent>().Rotation.LengthSq() < 0.000001f);

    ClearEditorCommandHistory();
    SetEditorCommandHistoryScene(nullptr);
    SetEditorGizmoOperation(EditorGizmoOperation::Translate);
    SetEditorGizmoSpace(EditorGizmoSpace::World);
}

} // namespace

void RunEditorViewportGeometrySelfTests()
{
    EditorViewportRenderTargetExtent extent;

    RHIOffscreenRenderTargetSpecification renderTargetSpecification;
    renderTargetSpecification.Width = 1280u;
    renderTargetSpecification.Height = 720u;
    assert(IsValidRHIOffscreenRenderTargetSpecification(
        renderTargetSpecification) == true);
    renderTargetSpecification.Width = 0u;
    assert(IsValidRHIOffscreenRenderTargetSpecification(
        renderTargetSpecification) == false);
    renderTargetSpecification.Width = 1280u;
    renderTargetSpecification.ColorFormat = RHITextureFormat::R32I;
    assert(IsValidRHIOffscreenRenderTargetSpecification(
        renderTargetSpecification) == false);

    // UI論理座標へ150%のWindow framebuffer倍率を一度だけ適用します。
    assert(TryCalculateEditorViewportRenderTargetExtent(
        math::Vec2(640.0f, 360.0f),
        math::Vec2(1280.0f, 720.0f),
        math::Vec2(1920.0f, 1080.0f), extent) == true);
    assert(extent.Width == 960u);
    assert(extent.Height == 540u);

    // 非整数Pixelは切り捨てると表示端が不足するため、RenderTarget側を切り上げます。
    assert(TryCalculateEditorViewportRenderTargetExtent(
        math::Vec2(100.25f, 50.25f),
        math::Vec2(800.0f, 600.0f),
        math::Vec2(1000.0f, 750.0f), extent) == true);
    assert(extent.Width == 126u);
    assert(extent.Height == 63u);

    // Window外へResizeされた論理Panelは、実際に表示可能なFramebuffer範囲までに制限します。
    assert(TryCalculateEditorViewportRenderTargetExtent(
        math::Vec2(2000.0f, 1200.0f),
        math::Vec2(800.0f, 600.0f),
        math::Vec2(1600.0f, 1200.0f), extent) == true);
    assert(extent.Width == 1600u);
    assert(extent.Height == 1200u);
    assert(extent.LogicalSize.x == 800.0f);
    assert(extent.LogicalSize.y == 600.0f);

    // 最小化・0 Content・非数はResize要求を作らず、既存Attachmentを保持します。
    assert(TryCalculateEditorViewportRenderTargetExtent(
        math::Vec2(640.0f, 360.0f),
        math::Vec2(1280.0f, 720.0f),
        math::Vec2(0.0f, 0.0f), extent) == false);
    assert(TryCalculateEditorViewportRenderTargetExtent(
        math::Vec2(0.0f, 360.0f),
        math::Vec2(1280.0f, 720.0f),
        math::Vec2(1280.0f, 720.0f), extent) == false);
    assert(TryCalculateEditorViewportRenderTargetExtent(
        math::Vec2(std::numeric_limits<float>::quiet_NaN(), 360.0f),
        math::Vec2(1280.0f, 720.0f),
        math::Vec2(1280.0f, 720.0f), extent) == false);

    RunEditorViewportInputRegionSelfTests();
    RunEditorGizmoGeometrySelfTests();
    RunEditorGizmoControllerSelfTests();
}

} // namespace Raven::tests
