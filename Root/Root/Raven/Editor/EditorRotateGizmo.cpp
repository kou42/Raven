#include "Raven/Editor/EditorRotateGizmo.h"

#include "Raven/Editor/EditorCamera.h"
#include "Raven/Editor/EditorGizmo.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathQuatanion.h"
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

enum class RotateAxis
{
    None = 0,
    X,
    Y,
    Z
};

struct RotateGizmoState
{
    RotateAxis ActiveAxis = RotateAxis::None;
    std::uint64_t EntityValue = 0;
    Scene* EntityScene = nullptr;

    math::Vec3 DragStartRotation{};
    math::Vec2 DragStartDirection{};
};

RotateGizmoState s_GizmoState{};

void ResetGizmoState()
{
    s_GizmoState = RotateGizmoState{};
}

int GetAxisIndex(RotateAxis axis)
{
    switch (axis)
    {
    case RotateAxis::X:
        return 0;
    case RotateAxis::Y:
        return 1;
    case RotateAxis::Z:
        return 2;
    case RotateAxis::None:
    default:
        return -1;
    }
}

math::Vec3 GetCanonicalAxisDirection(RotateAxis axis)
{
    switch (axis)
    {
    case RotateAxis::X:
        return { 1.0f, 0.0f, 0.0f };
    case RotateAxis::Y:
        return { 0.0f, 1.0f, 0.0f };
    case RotateAxis::Z:
        return { 0.0f, 0.0f, 1.0f };
    case RotateAxis::None:
    default:
        return {};
    }
}

ImU32 GetAxisColor(RotateAxis axis, bool highlighted)
{
    switch (axis)
    {
    case RotateAxis::X:
        return highlighted ? IM_COL32(255, 120, 120, 255) : IM_COL32(220, 70, 70, 255);
    case RotateAxis::Y:
        return highlighted ? IM_COL32(130, 255, 130, 255) : IM_COL32(70, 210, 90, 255);
    case RotateAxis::Z:
        return highlighted ? IM_COL32(120, 170, 255, 255) : IM_COL32(70, 120, 230, 255);
    case RotateAxis::None:
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

void BuildRingBasis(
    RotateAxis axis,
    const math::Vec3& rotation,
    math::Vec3& outU,
    math::Vec3& outV)
{
    // 各Ringは「操作軸を法線とする平面」上に生成します。
    // Worldでは固定基底、LocalではEntity Rotationで平面基底そのものを回転させます。
    switch (axis)
    {
    case RotateAxis::X:
        outU = { 0.0f, 1.0f, 0.0f };
        outV = { 0.0f, 0.0f, 1.0f };
        break;
    case RotateAxis::Y:
        outU = { 1.0f, 0.0f, 0.0f };
        outV = { 0.0f, 0.0f, 1.0f };
        break;
    case RotateAxis::Z:
        outU = { 1.0f, 0.0f, 0.0f };
        outV = { 0.0f, 1.0f, 0.0f };
        break;
    case RotateAxis::None:
    default:
        outU = {};
        outV = {};
        return;
    }

    if (GetEditorGizmoSpace() == EditorGizmoSpace::Local)
    {
        const math::Quat orientation = math::Quat::FromEulerXYZ(
            rotation.x,
            rotation.y,
            rotation.z);

        outU = orientation.Rotate(outU).Normalized();
        outV = orientation.Rotate(outV).Normalized();
    }
}

void ApplyRotationDelta(
    math::Vec3& rotation,
    const math::Vec3& dragStartRotation,
    RotateAxis axis,
    float delta)
{
    const math::Quat startOrientation = math::Quat::FromEulerXYZ(
        dragStartRotation.x,
        dragStartRotation.y,
        dragStartRotation.z);

    const math::Vec3 canonicalAxis = GetCanonicalAxisDirection(axis);
    if (canonicalAxis.LengthSq() <= math::Epsilon * math::Epsilon)
    {
        return;
    }

    const math::Quat deltaRotation = math::Quat::FromAxisAngle(
        canonicalAxis,
        delta);

    math::Quat resultOrientation{};

    if (GetEditorGizmoSpace() == EditorGizmoSpace::Local)
    {
        // Local回転は現在姿勢の「後ろ」にdeltaを掛けます。
        // これによりdelta軸はEntity自身のLocal X/Y/Zとして解釈されます。
        resultOrientation = startOrientation * deltaRotation;
    }
    else
    {
        // World回転は現在姿勢の「前」にdeltaを掛けます。
        // delta軸をWorld X/Y/Zとして適用するため、Local回転と乗算順序を分けます。
        resultOrientation = deltaRotation * startOrientation;
    }

    rotation = resultOrientation.Normalized().ToEulerXYZ();
}

} // namespace

bool RenderRotateGizmo(
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

    if (s_GizmoState.ActiveAxis != RotateAxis::None
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

    // Translate Gizmoと同じくCamera距離に比例してWorld半径を変え、Screen上の見かけサイズを
    // 大きく変えないようにします。RingはTranslate軸より少し大きくして掴みやすくします。
    const float cameraDistance = (originWorld - camera.GetPosition()).Length();
    const float ringWorldRadius = std::max(cameraDistance * 0.14f, 0.9f);

    constexpr int segmentCount = 64;
    constexpr float twoPi = 6.28318530718f;
    constexpr float hitRadius = 7.0f;

    struct RingScreenData
    {
        RotateAxis Axis = RotateAxis::None;
        math::Vec2 Points[segmentCount + 1]{};
        bool SegmentVisible[segmentCount]{};
        bool HasVisibleSegment = false;
    };

    RingScreenData rings[3] =
    {
        { RotateAxis::X },
        { RotateAxis::Y },
        { RotateAxis::Z }
    };

    for (RingScreenData& ring : rings)
    {
        math::Vec3 basisU{};
        math::Vec3 basisV{};
        BuildRingBasis(ring.Axis, transform.Rotation, basisU, basisV);

        bool pointVisible[segmentCount + 1]{};
        for (int i = 0; i <= segmentCount; ++i)
        {
            const float angle = twoPi * static_cast<float>(i) / static_cast<float>(segmentCount);
            const math::Vec3 worldPoint = originWorld
                + basisU * (std::cos(angle) * ringWorldRadius)
                + basisV * (std::sin(angle) * ringWorldRadius);

            pointVisible[i] = ProjectWorldToScreen(
                worldPoint,
                camera,
                viewportMinX,
                viewportMinY,
                viewportWidth,
                viewportHeight,
                ring.Points[i]);
        }

        for (int i = 0; i < segmentCount; ++i)
        {
            ring.SegmentVisible[i] = pointVisible[i] && pointVisible[i + 1];
            if (ring.SegmentVisible[i])
            {
                ring.HasVisibleSegment = true;
            }
        }
    }

    const ImVec2 mousePositionImGui = ImGui::GetMousePos();
    const math::Vec2 mousePosition{ mousePositionImGui.x, mousePositionImGui.y };

    RotateAxis hoveredAxis = RotateAxis::None;
    float closestDistance = hitRadius;

    if (s_GizmoState.ActiveAxis == RotateAxis::None)
    {
        for (const RingScreenData& ring : rings)
        {
            if (ring.HasVisibleSegment == false)
            {
                continue;
            }

            for (int i = 0; i < segmentCount; ++i)
            {
                if (ring.SegmentVisible[i] == false)
                {
                    continue;
                }

                const float distance = DistancePointToSegment(
                    mousePosition,
                    ring.Points[i],
                    ring.Points[i + 1]);

                if (distance <= closestDistance)
                {
                    closestDistance = distance;
                    hoveredAxis = ring.Axis;
                }
            }
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (drawList != nullptr)
    {
        for (const RingScreenData& ring : rings)
        {
            const bool highlighted = ring.Axis == hoveredAxis || ring.Axis == s_GizmoState.ActiveAxis;
            const ImU32 color = GetAxisColor(ring.Axis, highlighted);
            const float thickness = highlighted ? 4.0f : 2.5f;

            for (int i = 0; i < segmentCount; ++i)
            {
                if (ring.SegmentVisible[i] == false)
                {
                    continue;
                }

                drawList->AddLine(
                    ImVec2(ring.Points[i].x, ring.Points[i].y),
                    ImVec2(ring.Points[i + 1].x, ring.Points[i + 1].y),
                    color,
                    thickness);
            }
        }
    }

    bool consumedMouse = false;

    if (s_GizmoState.ActiveAxis == RotateAxis::None
        && hoveredAxis != RotateAxis::None
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const math::Vec2 fromCenter = mousePosition - originScreen;
        if (fromCenter.LengthSq() > math::Epsilon * math::Epsilon)
        {
            s_GizmoState.ActiveAxis = hoveredAxis;
            s_GizmoState.EntityValue = selectedValue;
            s_GizmoState.EntityScene = selectedScene;
            s_GizmoState.DragStartRotation = transform.Rotation;
            s_GizmoState.DragStartDirection = fromCenter.Normalized();
            consumedMouse = true;
        }
    }

    if (s_GizmoState.ActiveAxis != RotateAxis::None)
    {
        consumedMouse = true;

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) == false)
        {
            ResetGizmoState();
            return true;
        }

        const math::Vec2 currentFromCenter = mousePosition - originScreen;
        if (currentFromCenter.LengthSq() <= math::Epsilon * math::Epsilon)
        {
            return true;
        }

        const math::Vec2 currentDirection = currentFromCenter.Normalized();

        // atan2(cross,dot)により開始方向から現在方向までの符号付き角度を[-pi,+pi]で求めます。
        // MouseのScreen Yは下向きが正なので、World回転として直感的な向きになるよう符号を反転します。
        const float cross = math::Vec2::Cross(s_GizmoState.DragStartDirection, currentDirection);
        const float dot = math::Vec2::Dot(s_GizmoState.DragStartDirection, currentDirection);
        const float angleDelta = -std::atan2(cross, dot);

        ApplyRotationDelta(
            transform.Rotation,
            s_GizmoState.DragStartRotation,
            s_GizmoState.ActiveAxis,
            angleDelta);
    }

    return consumedMouse;
}

} // namespace Raven
