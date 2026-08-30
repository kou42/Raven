#include "Raven/Editor/EditorTranslateGizmo.h"

#include "Raven/Editor/EditorCamera.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Raven
{
namespace
{

enum class TranslateAxis
{
    None = 0,
    X,
    Y,
    Z
};

struct TranslateGizmoState
{
    TranslateAxis ActiveAxis = TranslateAxis::None;

    // Drag開始後にEntity選択が変わった場合、別Entityへ同じDragを誤適用しないため
    // Handle値とSceneの両方を記録します。
    std::uint64_t EntityValue = 0;
    Scene* EntityScene = nullptr;

    math::Vec2 DragStartMouse{};
    math::Vec3 DragStartPosition{};

    // Mouse DragをWorld軸移動へ変換するため、Drag開始時点のScreen上の軸方向と
    // 1 world unitあたりのpixel長を固定して保持します。
    math::Vec2 ScreenAxisDirection{};
    float PixelsPerWorldUnit = 0.0f;
};

TranslateGizmoState s_GizmoState{};

void ResetGizmoState()
{
    s_GizmoState = TranslateGizmoState{};
}

math::Vec3 GetAxisDirection(TranslateAxis axis)
{
    switch (axis)
    {
    case TranslateAxis::X:
        return { 1.0f, 0.0f, 0.0f };

    case TranslateAxis::Y:
        return { 0.0f, 1.0f, 0.0f };

    case TranslateAxis::Z:
        return { 0.0f, 0.0f, 1.0f };

    case TranslateAxis::None:
    default:
        return {};
    }
}

ImU32 GetAxisColor(TranslateAxis axis, bool highlighted)
{
    // Editor Gizmoでは一般的なX=Red / Y=Green / Z=Blueを使用します。
    // Hover / Drag中は明度を上げ、現在操作対象の軸を視覚的に判別しやすくします。
    switch (axis)
    {
    case TranslateAxis::X:
        return highlighted
            ? IM_COL32(255, 120, 120, 255)
            : IM_COL32(220, 70, 70, 255);

    case TranslateAxis::Y:
        return highlighted
            ? IM_COL32(130, 255, 130, 255)
            : IM_COL32(70, 210, 90, 255);

    case TranslateAxis::Z:
        return highlighted
            ? IM_COL32(120, 170, 255, 255)
            : IM_COL32(70, 120, 230, 255);

    case TranslateAxis::None:
    default:
        return IM_COL32(255, 255, 255, 255);
    }
}

bool ProjectWorldToScreen(
    const math::Vec3& worldPosition,
    const EditorCamera& camera,
    float viewportMinX,
    float viewportMinY,
    float viewportWidth,
    float viewportHeight,
    math::Vec2& outScreenPosition)
{
    const math::Mat4 viewProjection =
        camera.GetProjectionMatrix() * camera.GetViewMatrix();

    const math::Vec4 clip =
        viewProjection * math::Vec4(worldPosition, 1.0f);

    // Cameraの背面、またはwがほぼ0の点はPerspective Divideできないため描画対象外です。
    if (clip.w <= math::Epsilon)
    {
        return false;
    }

    const float inverseW = 1.0f / clip.w;
    const float ndcX = clip.x * inverseW;
    const float ndcY = clip.y * inverseW;

    outScreenPosition.x =
        viewportMinX + ((ndcX * 0.5f) + 0.5f) * viewportWidth;

    // NDCは上方向が+Y、Dear ImGuiはScreen下方向が+Yなので反転します。
    outScreenPosition.y =
        viewportMinY + (1.0f - ((ndcY * 0.5f) + 0.5f)) * viewportHeight;

    return true;
}

float DistancePointToSegment(
    const math::Vec2& point,
    const math::Vec2& start,
    const math::Vec2& end)
{
    const math::Vec2 segment = end - start;
    const float segmentLengthSq = segment.LengthSq();

    if (segmentLengthSq <= math::Epsilon * math::Epsilon)
    {
        return (point - start).Length();
    }

    const float projection =
        math::Vec2::Dot(point - start, segment) / segmentLengthSq;
    const float t = std::clamp(projection, 0.0f, 1.0f);
    const math::Vec2 closest = start + segment * t;
    return (point - closest).Length();
}

} // namespace

bool RenderTranslateGizmo(
    Entity selectedEntity,
    const EditorCamera& camera,
    float viewportMinX,
    float viewportMinY,
    float viewportMaxX,
    float viewportMaxY)
{
    const float viewportWidth = viewportMaxX - viewportMinX;
    const float viewportHeight = viewportMaxY - viewportMinY;

    if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
    {
        ResetGizmoState();
        return false;
    }

    if (static_cast<bool>(selectedEntity) == false
        || selectedEntity.GetScene() == nullptr
        || selectedEntity.GetScene()->IsEntityAlive(selectedEntity) == false
        || selectedEntity.HasComponent<TransformComponent>() == false)
    {
        ResetGizmoState();
        return false;
    }

    const std::uint64_t selectedValue = selectedEntity.GetValue();
    Scene* selectedScene = selectedEntity.GetScene();

    // Drag中にHierarchy/Pickingで別Entityへ選択が移った場合は、古いDrag状態を即座に破棄します。
    if (s_GizmoState.ActiveAxis != TranslateAxis::None
        && (s_GizmoState.EntityValue != selectedValue
            || s_GizmoState.EntityScene != selectedScene))
    {
        ResetGizmoState();
    }

    TransformComponent& transform =
        selectedEntity.GetComponent<TransformComponent>();

    const math::Vec3 originWorld = transform.Position;

    math::Vec2 originScreen{};
    if (ProjectWorldToScreen(
            originWorld,
            camera,
            viewportMinX,
            viewportMinY,
            viewportWidth,
            viewportHeight,
            originScreen) == false)
    {
        ResetGizmoState();
        return false;
    }

    // Perspective Cameraからの距離に比例してWorld上の軸長を変えます。
    // Screen上では概ね一定サイズに見えるため、遠距離EntityでもGizmoが極端に小さくなりません。
    const float cameraDistance =
        (originWorld - camera.GetPosition()).Length();
    const float gizmoWorldLength =
        std::max(cameraDistance * 0.12f, 0.75f);

    struct AxisScreenData
    {
        TranslateAxis Axis = TranslateAxis::None;
        math::Vec3 WorldDirection{};
        math::Vec2 EndScreen{};
        math::Vec2 ScreenVector{};
        float ScreenLength = 0.0f;
        bool Visible = false;
    };

    AxisScreenData axes[3] =
    {
        { TranslateAxis::X, { 1.0f, 0.0f, 0.0f } },
        { TranslateAxis::Y, { 0.0f, 1.0f, 0.0f } },
        { TranslateAxis::Z, { 0.0f, 0.0f, 1.0f } }
    };

    for (AxisScreenData& axis : axes)
    {
        const math::Vec3 axisEndWorld =
            originWorld + axis.WorldDirection * gizmoWorldLength;

        axis.Visible = ProjectWorldToScreen(
            axisEndWorld,
            camera,
            viewportMinX,
            viewportMinY,
            viewportWidth,
            viewportHeight,
            axis.EndScreen);

        if (axis.Visible == false)
        {
            continue;
        }

        axis.ScreenVector = axis.EndScreen - originScreen;
        axis.ScreenLength = axis.ScreenVector.Length();

        // Camera方向とほぼ平行な軸はScreen上で点に潰れます。
        // その状態ではDrag量を安定してWorldへ戻せないため操作対象から外します。
        if (axis.ScreenLength < 8.0f)
        {
            axis.Visible = false;
        }
    }

    const ImVec2 mousePositionImGui = ImGui::GetMousePos();
    const math::Vec2 mousePosition{
        mousePositionImGui.x,
        mousePositionImGui.y
    };

    constexpr float hitRadius = 8.0f;
    TranslateAxis hoveredAxis = TranslateAxis::None;
    float closestDistance = hitRadius;

    if (s_GizmoState.ActiveAxis == TranslateAxis::None)
    {
        for (const AxisScreenData& axis : axes)
        {
            if (axis.Visible == false)
            {
                continue;
            }

            const float distance = DistancePointToSegment(
                mousePosition,
                originScreen,
                axis.EndScreen);

            if (distance <= closestDistance)
            {
                closestDistance = distance;
                hoveredAxis = axis.Axis;
            }
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (drawList != nullptr)
    {
        for (const AxisScreenData& axis : axes)
        {
            if (axis.Visible == false)
            {
                continue;
            }

            const bool highlighted =
                axis.Axis == hoveredAxis
                || axis.Axis == s_GizmoState.ActiveAxis;

            const float thickness = highlighted ? 5.0f : 3.0f;
            const ImU32 color = GetAxisColor(axis.Axis, highlighted);

            drawList->AddLine(
                ImVec2(originScreen.x, originScreen.y),
                ImVec2(axis.EndScreen.x, axis.EndScreen.y),
                color,
                thickness);

            // 軸終端へ小さなHandleを描き、Lineだけよりクリック対象を認識しやすくします。
            drawList->AddCircleFilled(
                ImVec2(axis.EndScreen.x, axis.EndScreen.y),
                highlighted ? 6.0f : 5.0f,
                color);
        }
    }

    bool consumedMouse = false;

    if (s_GizmoState.ActiveAxis == TranslateAxis::None
        && hoveredAxis != TranslateAxis::None
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        for (const AxisScreenData& axis : axes)
        {
            if (axis.Axis != hoveredAxis || axis.Visible == false)
            {
                continue;
            }

            s_GizmoState.ActiveAxis = hoveredAxis;
            s_GizmoState.EntityValue = selectedValue;
            s_GizmoState.EntityScene = selectedScene;
            s_GizmoState.DragStartMouse = mousePosition;
            s_GizmoState.DragStartPosition = transform.Position;
            s_GizmoState.ScreenAxisDirection =
                axis.ScreenVector / axis.ScreenLength;
            s_GizmoState.PixelsPerWorldUnit =
                axis.ScreenLength / gizmoWorldLength;

            consumedMouse = true;
            break;
        }
    }

    if (s_GizmoState.ActiveAxis != TranslateAxis::None)
    {
        consumedMouse = true;

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) == false)
        {
            ResetGizmoState();
            return true;
        }

        if (s_GizmoState.PixelsPerWorldUnit <= math::Epsilon)
        {
            ResetGizmoState();
            return true;
        }

        const math::Vec2 mouseDelta =
            mousePosition - s_GizmoState.DragStartMouse;

        const float projectedPixels = math::Vec2::Dot(
            mouseDelta,
            s_GizmoState.ScreenAxisDirection);

        const float worldDelta =
            projectedPixels / s_GizmoState.PixelsPerWorldUnit;

        const math::Vec3 activeWorldAxis =
            GetAxisDirection(s_GizmoState.ActiveAxis);

        transform.Position =
            s_GizmoState.DragStartPosition
            + activeWorldAxis * worldDelta;
    }

    return consumedMouse;
}

} // namespace Raven
