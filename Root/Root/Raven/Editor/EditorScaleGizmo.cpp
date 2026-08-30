#include "Raven/Editor/EditorScaleGizmo.h"

#include "Raven/Editor/EditorCamera.h"
#include "Raven/Editor/Command/TransformCommand.h"
#include "Raven/Editor/EditorCommandHistory.h"
#include "Raven/Editor/EditorGizmo.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace Raven
{
namespace
{

enum class ScaleAxis
{
    None = 0,
    X,
    Y,
    Z
};

struct ScaleGizmoState
{
    ScaleAxis ActiveAxis = ScaleAxis::None;
    std::uint64_t EntityValue = 0;
    Scene* EntityScene = nullptr;

    math::Vec2 DragStartMouse{};
    math::Vec3 DragStartScale{ 1.0f, 1.0f, 1.0f };
    math::Vec2 ScreenAxisDirection{};
    float PixelsPerScaleUnit = 0.0f;
};

ScaleGizmoState s_GizmoState{};

void ResetGizmoState()
{
    s_GizmoState = ScaleGizmoState{};
}

int GetAxisIndex(ScaleAxis axis)
{
    switch (axis)
    {
    case ScaleAxis::X:
        return 0;
    case ScaleAxis::Y:
        return 1;
    case ScaleAxis::Z:
        return 2;
    case ScaleAxis::None:
    default:
        return -1;
    }
}

ImU32 GetAxisColor(ScaleAxis axis, bool highlighted)
{
    switch (axis)
    {
    case ScaleAxis::X:
        return highlighted ? IM_COL32(255, 120, 120, 255) : IM_COL32(220, 70, 70, 255);
    case ScaleAxis::Y:
        return highlighted ? IM_COL32(130, 255, 130, 255) : IM_COL32(70, 210, 90, 255);
    case ScaleAxis::Z:
        return highlighted ? IM_COL32(120, 170, 255, 255) : IM_COL32(70, 120, 230, 255);
    case ScaleAxis::None:
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
    const math::Mat4 viewProjection = camera.GetProjectionMatrix() * camera.GetViewMatrix();
    const math::Vec4 clip = viewProjection * math::Vec4(worldPosition, 1.0f);

    if (clip.w <= math::Epsilon)
    {
        return false;
    }

    const float inverseW = 1.0f / clip.w;
    const float ndcX = clip.x * inverseW;
    const float ndcY = clip.y * inverseW;

    outScreenPosition.x = viewportMinX + ((ndcX * 0.5f) + 0.5f) * viewportWidth;
    outScreenPosition.y = viewportMinY + (1.0f - ((ndcY * 0.5f) + 0.5f)) * viewportHeight;
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

    const float projection = math::Vec2::Dot(point - start, segment) / segmentLengthSq;
    const float t = std::clamp(projection, 0.0f, 1.0f);
    return (point - (start + segment * t)).Length();
}

float ApplyMinimumScale(float value, float originalValue)
{
    // Scale=0はModel Matrixを退化させるため避けます。
    // Drag中に符号反転まで許可すると0を跨ぐ瞬間に必ず退化するので、現在は開始時の符号を維持し、
    // 最小絶対値でClampします。負Scaleを持つ既存Entityは負のまま編集できます。
    constexpr float minimumAbsoluteScale = 0.01f;
    const float sign = originalValue < 0.0f ? -1.0f : 1.0f;
    const float signedMinimum = minimumAbsoluteScale * sign;

    if (sign > 0.0f && value < minimumAbsoluteScale)
    {
        return signedMinimum;
    }

    if (sign < 0.0f && value > -minimumAbsoluteScale)
    {
        return signedMinimum;
    }

    return value;
}

void SetScaleAxisValue(math::Vec3& scale, ScaleAxis axis, float value, const math::Vec3& originalScale)
{
    switch (axis)
    {
    case ScaleAxis::X:
        scale.x = ApplyMinimumScale(value, originalScale.x);
        break;
    case ScaleAxis::Y:
        scale.y = ApplyMinimumScale(value, originalScale.y);
        break;
    case ScaleAxis::Z:
        scale.z = ApplyMinimumScale(value, originalScale.z);
        break;
    case ScaleAxis::None:
    default:
        break;
    }
}

float GetScaleAxisValue(const math::Vec3& scale, ScaleAxis axis)
{
    switch (axis)
    {
    case ScaleAxis::X:
        return scale.x;
    case ScaleAxis::Y:
        return scale.y;
    case ScaleAxis::Z:
        return scale.z;
    case ScaleAxis::None:
    default:
        return 1.0f;
    }
}

} // namespace

bool RenderScaleGizmo(
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

    if (s_GizmoState.ActiveAxis != ScaleAxis::None
        && (s_GizmoState.EntityValue != selectedValue
            || s_GizmoState.EntityScene != selectedScene))
    {
        ResetGizmoState();
    }

    TransformComponent& transform = selectedEntity.GetComponent<TransformComponent>();
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

    const float cameraDistance = (originWorld - camera.GetPosition()).Length();
    const float gizmoWorldLength = std::max(cameraDistance * 0.12f, 0.75f);

    struct AxisScreenData
    {
        ScaleAxis Axis = ScaleAxis::None;
        math::Vec3 WorldDirection{};
        math::Vec2 EndScreen{};
        math::Vec2 ScreenVector{};
        float ScreenLength = 0.0f;
        bool Visible = false;
    };

    AxisScreenData axes[3] =
    {
        { ScaleAxis::X },
        { ScaleAxis::Y },
        { ScaleAxis::Z }
    };

    for (AxisScreenData& axis : axes)
    {
        // Worldでは固定X/Y/Z、LocalではEntity Rotationへ追従した軸を描画します。
        // Scale値そのものはTransformComponentのX/Y/Z成分なので、選択した色/軸と同じ成分だけを更新します。
        axis.WorldDirection = GetEditorGizmoAxisDirection(
            transform.Rotation,
            GetAxisIndex(axis.Axis));

        axis.Visible = ProjectWorldToScreen(
            originWorld + axis.WorldDirection * gizmoWorldLength,
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

        if (axis.ScreenLength < 8.0f)
        {
            axis.Visible = false;
        }
    }

    const ImVec2 mousePositionImGui = ImGui::GetMousePos();
    const math::Vec2 mousePosition{ mousePositionImGui.x, mousePositionImGui.y };

    constexpr float hitRadius = 8.0f;
    ScaleAxis hoveredAxis = ScaleAxis::None;
    float closestDistance = hitRadius;

    if (s_GizmoState.ActiveAxis == ScaleAxis::None)
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

            const bool highlighted = axis.Axis == hoveredAxis || axis.Axis == s_GizmoState.ActiveAxis;
            const ImU32 color = GetAxisColor(axis.Axis, highlighted);

            drawList->AddLine(
                ImVec2(originScreen.x, originScreen.y),
                ImVec2(axis.EndScreen.x, axis.EndScreen.y),
                color,
                highlighted ? 5.0f : 3.0f);

            // Translateは丸Handle、Scaleは四角Handleにして視覚的に操作種別を区別します。
            const float halfSize = highlighted ? 6.0f : 5.0f;
            drawList->AddRectFilled(
                ImVec2(axis.EndScreen.x - halfSize, axis.EndScreen.y - halfSize),
                ImVec2(axis.EndScreen.x + halfSize, axis.EndScreen.y + halfSize),
                color);
        }
    }

    bool consumedMouse = false;

    if (s_GizmoState.ActiveAxis == ScaleAxis::None
        && hoveredAxis != ScaleAxis::None
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
            s_GizmoState.DragStartScale = transform.Scale;
            s_GizmoState.ScreenAxisDirection = axis.ScreenVector / axis.ScreenLength;

            // 軸のScreen長を「Scale 1.0を変化させるDrag距離」として扱います。
            // Camera距離によらず、Gizmo Handle一本分のDragで概ねScaleが1増減します。
            s_GizmoState.PixelsPerScaleUnit = axis.ScreenLength;
            consumedMouse = true;
            break;
        }
    }

    if (s_GizmoState.ActiveAxis != ScaleAxis::None)
    {
        consumedMouse = true;

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) == false)
        {
            // ScaleもMouse Release時に1回だけ履歴へ記録します。
            TransformComponent beforeTransform = transform;
            beforeTransform.Scale = s_GizmoState.DragStartScale;

            const Entity editedEntity(
                EntityHandle::FromValue(s_GizmoState.EntityValue),
                s_GizmoState.EntityScene);

            // Drag中に適用済みのScaleを二重実行せず、1 Dragを1 Commandとして登録します。
            RecordAlreadyExecutedEditorCommand(
                std::make_unique<TransformCommand>(editedEntity, beforeTransform, transform));

            ResetGizmoState();
            return true;
        }

        if (s_GizmoState.PixelsPerScaleUnit <= math::Epsilon)
        {
            ResetGizmoState();
            return true;
        }

        const math::Vec2 mouseDelta = mousePosition - s_GizmoState.DragStartMouse;
        const float projectedPixels = math::Vec2::Dot(mouseDelta, s_GizmoState.ScreenAxisDirection);
        float scaleDelta = projectedPixels / s_GizmoState.PixelsPerScaleUnit;

        // Scaleも絶対Scale値ではなくDrag開始値からのdeltaをSnapします。
        // 例えば開始Scale=1.03ならCtrl操作は1.13, 1.23...となり、1.0へ突然吸着しません。
        if (ImGui::GetIO().KeyCtrl)
        {
            scaleDelta = ApplyEditorGizmoSnap(
                scaleDelta,
                GetEditorGizmoSnapSettings().ScaleStep);
        }

        const float originalAxisScale = GetScaleAxisValue(
            s_GizmoState.DragStartScale,
            s_GizmoState.ActiveAxis);

        transform.Scale = s_GizmoState.DragStartScale;
        SetScaleAxisValue(
            transform.Scale,
            s_GizmoState.ActiveAxis,
            originalAxisScale + scaleDelta,
            s_GizmoState.DragStartScale);
    }

    return consumedMouse;
}

} // namespace Raven
