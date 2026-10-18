#include "Raven/Editor/EditorGizmo.h"

#include "Raven/Editor/Command/TransformCommand.h"
#include "Raven/Editor/EditorCamera.h"
#include "Raven/Editor/EditorCommandHistory.h"
#include "Raven/Editor/EditorRotateGizmo.h"
#include "Raven/Editor/EditorScaleGizmo.h"
#include "Raven/Editor/EditorTranslateGizmo.h"
#include "Raven/Math/MathQuatanion.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace Raven
{
namespace
{

// Editor全体で現在選択中のGizmo操作を保持します。
// EditorLayerは操作モードの値を保持せず、UIからSet/Get APIだけを利用します。
EditorGizmoOperation s_Operation = EditorGizmoOperation::Translate;

// World / LocalもGizmo機能側で一元管理します。
// 初期値は従来挙動と同じWorldにして、既存Scene Viewの操作感を変更しません。
EditorGizmoSpace s_Space = EditorGizmoSpace::World;

// Translate / Rotate / Scaleで使用するSnap刻み幅もGizmo側へ集約します。
// 将来Editor Settings UIを追加した場合も、この値だけを更新すれば各Gizmoへ反映できます。
EditorGizmoSnapSettings s_SnapSettings{};

math::Vec3 GetCanonicalAxis(EditorGizmoAxis axis)
{
    switch (axis)
    {
    case EditorGizmoAxis::X: return math::Vec3(1.0f, 0.0f, 0.0f);
    case EditorGizmoAxis::Y: return math::Vec3(0.0f, 1.0f, 0.0f);
    case EditorGizmoAxis::Z: return math::Vec3(0.0f, 0.0f, 1.0f);
    case EditorGizmoAxis::None:
    default: return math::Vec3{};
    }
}

math::Vec3 GetAxisDirectionForSpace(
    const math::Vec3& rotation,
    EditorGizmoAxis axis,
    EditorGizmoSpace space)
{
    const math::Vec3 canonicalAxis = GetCanonicalAxis(axis);
    if (space == EditorGizmoSpace::World)
    {
        return canonicalAxis;
    }
    const math::Quat orientation = math::Quat::FromEulerXYZ(
        rotation.x, rotation.y, rotation.z);
    return orientation.Rotate(canonicalAxis).Normalized();
}

math::Vec4 GetAxisColor(EditorGizmoAxis axis, bool highlighted)
{
    switch (axis)
    {
    case EditorGizmoAxis::X:
        return highlighted
            ? math::Vec4(1.0f, 0.47f, 0.47f, 1.0f)
            : math::Vec4(0.86f, 0.27f, 0.27f, 1.0f);
    case EditorGizmoAxis::Y:
        return highlighted
            ? math::Vec4(0.51f, 1.0f, 0.51f, 1.0f)
            : math::Vec4(0.27f, 0.82f, 0.35f, 1.0f);
    case EditorGizmoAxis::Z:
        return highlighted
            ? math::Vec4(0.47f, 0.67f, 1.0f, 1.0f)
            : math::Vec4(0.27f, 0.47f, 0.90f, 1.0f);
    case EditorGizmoAxis::None:
    default:
        return math::Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

bool IsEditableGizmoEntity(Entity entity)
{
    return static_cast<bool>(entity) == true &&
        entity.GetScene() != nullptr &&
        entity.GetScene()->IsEntityAlive(entity) == true &&
        entity.HasComponent<TransformComponent>() == true;
}

struct EditorGizmoAxisGeometry
{
    EditorGizmoAxis Axis = EditorGizmoAxis::None;
    math::Vec3 WorldDirection{};
    math::Vec2 EndScreen{};
    math::Vec2 ScreenVector{};
    float ScreenLength = 0.0f;
    bool Visible = false;
};

struct EditorGizmoRingGeometry
{
    EditorGizmoAxis Axis = EditorGizmoAxis::None;
    std::vector<math::Vec2> Points;
    std::vector<bool> SegmentVisible;
    bool HasVisibleSegment = false;
};

struct EditorGizmoGeometry
{
    math::Vec2 OriginScreen{};
    EditorGizmoAxisGeometry Axes[3] =
    {
        { EditorGizmoAxis::X },
        { EditorGizmoAxis::Y },
        { EditorGizmoAxis::Z }
    };
    EditorGizmoRingGeometry Rings[3] =
    {
        { EditorGizmoAxis::X },
        { EditorGizmoAxis::Y },
        { EditorGizmoAxis::Z }
    };
    float WorldLength = 0.0f;
};

void BuildRingBasis(
    EditorGizmoAxis axis,
    const math::Vec3& rotation,
    EditorGizmoSpace space,
    math::Vec3& outU,
    math::Vec3& outV)
{
    switch (axis)
    {
    case EditorGizmoAxis::X:
        outU = math::Vec3(0.0f, 1.0f, 0.0f);
        outV = math::Vec3(0.0f, 0.0f, 1.0f);
        break;
    case EditorGizmoAxis::Y:
        outU = math::Vec3(1.0f, 0.0f, 0.0f);
        outV = math::Vec3(0.0f, 0.0f, 1.0f);
        break;
    case EditorGizmoAxis::Z:
        outU = math::Vec3(1.0f, 0.0f, 0.0f);
        outV = math::Vec3(0.0f, 1.0f, 0.0f);
        break;
    case EditorGizmoAxis::None:
    default:
        outU = math::Vec3{};
        outV = math::Vec3{};
        return;
    }

    if (space == EditorGizmoSpace::Local)
    {
        const math::Quat orientation = math::Quat::FromEulerXYZ(
            rotation.x, rotation.y, rotation.z);
        outU = orientation.Rotate(outU).Normalized();
        outV = orientation.Rotate(outV).Normalized();
    }
}

bool BuildGizmoGeometry(
    Entity entity,
    const EditorCamera& camera,
    const EditorGizmoViewportRect& viewport,
    EditorGizmoOperation operation,
    EditorGizmoSpace space,
    EditorGizmoGeometry& outGeometry)
{
    const float width = viewport.Max.x - viewport.Min.x;
    const float height = viewport.Max.y - viewport.Min.y;
    if (width <= 0.0f || height <= 0.0f || IsEditableGizmoEntity(entity) == false)
    {
        return false;
    }

    const TransformComponent& transform = entity.GetComponent<TransformComponent>();
    const math::Mat4 viewProjection =
        camera.GetProjectionMatrix() * camera.GetViewMatrix();
    if (ProjectEditorGizmoWorldToViewport(
        transform.Position, viewProjection, viewport, outGeometry.OriginScreen) == false)
    {
        return false;
    }

    const float cameraDistance =
        (transform.Position - camera.GetPosition()).Length();
    outGeometry.WorldLength = operation == EditorGizmoOperation::Rotate
        ? std::max(cameraDistance * 0.14f, 0.9f)
        : std::max(cameraDistance * 0.12f, 0.75f);

    if (operation != EditorGizmoOperation::Rotate)
    {
        for (EditorGizmoAxisGeometry& axis : outGeometry.Axes)
        {
            axis.WorldDirection = GetAxisDirectionForSpace(
                transform.Rotation, axis.Axis, space);
            axis.Visible = ProjectEditorGizmoWorldToViewport(
                transform.Position + axis.WorldDirection * outGeometry.WorldLength,
                viewProjection,
                viewport,
                axis.EndScreen);
            if (axis.Visible == false)
            {
                continue;
            }
            axis.ScreenVector = axis.EndScreen - outGeometry.OriginScreen;
            axis.ScreenLength = axis.ScreenVector.Length();
            if (axis.ScreenLength < 8.0f)
            {
                axis.Visible = false;
            }
        }
        return true;
    }

    constexpr int segmentCount = 64;
    constexpr float twoPi = 6.28318530718f;
    for (EditorGizmoRingGeometry& ring : outGeometry.Rings)
    {
        ring.Points.resize(static_cast<std::size_t>(segmentCount + 1));
        ring.SegmentVisible.resize(static_cast<std::size_t>(segmentCount));
        std::vector<bool> pointVisible(static_cast<std::size_t>(segmentCount + 1), false);
        math::Vec3 basisU{};
        math::Vec3 basisV{};
        BuildRingBasis(ring.Axis, transform.Rotation, space, basisU, basisV);
        for (int index = 0; index <= segmentCount; ++index)
        {
            const float angle = twoPi * static_cast<float>(index) /
                static_cast<float>(segmentCount);
            const math::Vec3 worldPoint = transform.Position +
                basisU * (std::cos(angle) * outGeometry.WorldLength) +
                basisV * (std::sin(angle) * outGeometry.WorldLength);
            pointVisible[static_cast<std::size_t>(index)] =
                ProjectEditorGizmoWorldToViewport(
                    worldPoint,
                    viewProjection,
                    viewport,
                    ring.Points[static_cast<std::size_t>(index)]);
        }
        for (int index = 0; index < segmentCount; ++index)
        {
            const bool visible =
                pointVisible[static_cast<std::size_t>(index)] == true &&
                pointVisible[static_cast<std::size_t>(index + 1)] == true;
            ring.SegmentVisible[static_cast<std::size_t>(index)] = visible;
            ring.HasVisibleSegment = ring.HasVisibleSegment || visible;
        }
    }
    return true;
}

EditorGizmoAxis HitTestGizmo(
    const EditorGizmoGeometry& geometry,
    EditorGizmoOperation operation,
    const math::Vec2& pointerPosition)
{
    const float hitRadius = operation == EditorGizmoOperation::Rotate ? 7.0f : 8.0f;
    float closestDistance = hitRadius;
    EditorGizmoAxis hoveredAxis = EditorGizmoAxis::None;
    if (operation != EditorGizmoOperation::Rotate)
    {
        for (const EditorGizmoAxisGeometry& axis : geometry.Axes)
        {
            if (axis.Visible == false)
            {
                continue;
            }
            const float distance = DistancePointToEditorGizmoSegment(
                pointerPosition, geometry.OriginScreen, axis.EndScreen);
            if (distance <= closestDistance)
            {
                closestDistance = distance;
                hoveredAxis = axis.Axis;
            }
        }
        return hoveredAxis;
    }

    for (const EditorGizmoRingGeometry& ring : geometry.Rings)
    {
        if (ring.HasVisibleSegment == false)
        {
            continue;
        }
        for (std::size_t index = 0u; index < ring.SegmentVisible.size(); ++index)
        {
            if (ring.SegmentVisible[index] == false)
            {
                continue;
            }
            const float distance = DistancePointToEditorGizmoSegment(
                pointerPosition, ring.Points[index], ring.Points[index + 1u]);
            if (distance <= closestDistance)
            {
                closestDistance = distance;
                hoveredAxis = ring.Axis;
            }
        }
    }
    return hoveredAxis;
}

float GetScaleAxisValue(const math::Vec3& scale, EditorGizmoAxis axis)
{
    if (axis == EditorGizmoAxis::X) { return scale.x; }
    if (axis == EditorGizmoAxis::Y) { return scale.y; }
    if (axis == EditorGizmoAxis::Z) { return scale.z; }
    return 1.0f;
}

void SetScaleAxisValue(
    math::Vec3& scale,
    EditorGizmoAxis axis,
    float value,
    const math::Vec3& originalScale)
{
    if (axis == EditorGizmoAxis::X)
    {
        scale.x = ApplyEditorGizmoMinimumScale(value, originalScale.x);
    }
    else if (axis == EditorGizmoAxis::Y)
    {
        scale.y = ApplyEditorGizmoMinimumScale(value, originalScale.y);
    }
    else if (axis == EditorGizmoAxis::Z)
    {
        scale.z = ApplyEditorGizmoMinimumScale(value, originalScale.z);
    }
}

bool TryBuildGizmoPointerRay(
    const EditorCamera& camera,
    const EditorGizmoViewportRect& viewport,
    const math::Vec2& pointerPosition,
    math::Vec3& outOrigin,
    math::Vec3& outDirection)
{
    const float width = viewport.Max.x - viewport.Min.x;
    const float height = viewport.Max.y - viewport.Min.y;
    if (width <= 0.0f || height <= 0.0f)
    {
        return false;
    }
    const float normalizedX = (pointerPosition.x - viewport.Min.x) / width;
    const float normalizedY = (pointerPosition.y - viewport.Min.y) / height;
    return camera.TryBuildWorldRay(
        normalizedX, normalizedY, outOrigin, outDirection);
}

} // namespace

EditorGizmoOperation GetEditorGizmoOperation()
{
    return s_Operation;
}

void SetEditorGizmoOperation(EditorGizmoOperation operation)
{
    s_Operation = operation;
}

EditorGizmoSpace GetEditorGizmoSpace()
{
    return s_Space;
}

void SetEditorGizmoSpace(EditorGizmoSpace space)
{
    s_Space = space;
}

void ToggleEditorGizmoSpace()
{
    if (s_Space == EditorGizmoSpace::World)
    {
        s_Space = EditorGizmoSpace::Local;
    }
    else
    {
        s_Space = EditorGizmoSpace::World;
    }
}

const EditorGizmoSnapSettings& GetEditorGizmoSnapSettings()
{
    return s_SnapSettings;
}

void SetEditorGizmoSnapSettings(const EditorGizmoSnapSettings& settings)
{
    // 0以下のstepでは量子化できないため、設定値は正の値だけ受理します。
    // Editor Settings UI等から一部だけ不正値が渡されても、既存の有効な設定を維持します。
    if (settings.TranslateStep > 0.0f)
    {
        s_SnapSettings.TranslateStep = settings.TranslateStep;
    }
    if (settings.RotateStepRadians > 0.0f)
    {
        s_SnapSettings.RotateStepRadians = settings.RotateStepRadians;
    }
    if (settings.ScaleStep > 0.0f)
    {
        s_SnapSettings.ScaleStep = settings.ScaleStep;
    }
}

float ApplyEditorGizmoSnap(float delta, float step)
{
    if (step <= 0.0f)
    {
        return delta;
    }

    // Drag開始値ではなくdeltaを最寄りのstepへ丸めます。
    // 例: Position=0.23からCtrl Dragしても0.0/0.5へ強制移動せず、
    //     0.23 + 0.5, 0.23 + 1.0 ... のように開始位置を基準としてSnapします。
    return std::round(delta / step) * step;
}

math::Vec3 GetEditorGizmoAxisDirection(
    const math::Vec3& rotation,
    int axisIndex)
{
    math::Vec3 axis{};

    switch (axisIndex)
    {
    case 0:
        axis = { 1.0f, 0.0f, 0.0f };
        break;
    case 1:
        axis = { 0.0f, 1.0f, 0.0f };
        break;
    case 2:
        axis = { 0.0f, 0.0f, 1.0f };
        break;
    default:
        return {};
    }

    if (s_Space == EditorGizmoSpace::World)
    {
        return axis;
    }

    // TransformComponent::GetTransform()と同じEuler XYZ体系を使い、Entity Rotationを
    // Quaternionへ変換して基準軸を回転させます。Local Gizmoの見た目と実際の操作方向を
    // 同じ正規姿勢から求めることで、複数軸回転後も軸の不整合を避けます。
    const math::Quat orientation = math::Quat::FromEulerXYZ(
        rotation.x,
        rotation.y,
        rotation.z);

    return orientation.Rotate(axis).Normalized();
}

bool ProjectEditorGizmoWorldToViewport(
    const math::Vec3& worldPosition,
    const math::Mat4& viewProjection,
    const EditorGizmoViewportRect& viewport,
    math::Vec2& outPosition)
{
    const float width = viewport.Max.x - viewport.Min.x;
    const float height = viewport.Max.y - viewport.Min.y;
    if (width <= 0.0f || height <= 0.0f)
    {
        return false;
    }

    const math::Vec4 clip = viewProjection * math::Vec4(worldPosition, 1.0f);
    if (clip.w <= math::Epsilon)
    {
        return false;
    }

    const float inverseW = 1.0f / clip.w;
    const float ndcX = clip.x * inverseW;
    const float ndcY = clip.y * inverseW;
    outPosition.x = viewport.Min.x + ((ndcX * 0.5f) + 0.5f) * width;
    // Raven UIも左上原点なので、NDCの上向き+YをScreenの下向き+Yへ反転します。
    outPosition.y = viewport.Min.y +
        (1.0f - ((ndcY * 0.5f) + 0.5f)) * height;
    return true;
}

float DistancePointToEditorGizmoSegment(
    const math::Vec2& point,
    const math::Vec2& start,
    const math::Vec2& end)
{
    const math::Vec2 segment = end - start;
    const float lengthSquared = segment.LengthSq();
    if (lengthSquared <= math::Epsilon * math::Epsilon)
    {
        return (point - start).Length();
    }
    const float projection =
        math::Vec2::Dot(point - start, segment) / lengthSquared;
    const float t = std::clamp(projection, 0.0f, 1.0f);
    return (point - (start + segment * t)).Length();
}

bool TryBuildEditorGizmoDragPlaneNormal(
    const math::Vec3& axisDirection,
    const math::Vec3& viewDirection,
    math::Vec3& outPlaneNormal)
{
    const math::Vec3 axis = axisDirection.Normalized();
    const math::Vec3 view = viewDirection.Normalized();
    if (axis.LengthSq() <= math::Epsilon * math::Epsilon ||
        view.LengthSq() <= math::Epsilon * math::Epsilon)
    {
        return false;
    }

    // Planeは操作軸を含む必要があるため、法線から軸方向成分を除きます。
    // Camera視線の軸直交成分を使うと、可能な範囲でCameraへ正対してRay交差が安定します。
    const math::Vec3 perpendicularView =
        view - axis * math::Vec3::Dot(view, axis);
    if (perpendicularView.LengthSq() <= 0.0001f)
    {
        // Cameraと操作軸がほぼ平行な場合、軸を含みつつRayとも交差する安定Planeは作れません。
        // 呼び出し側はScreen投影軸方式へfallbackします。
        return false;
    }
    outPlaneNormal = perpendicularView.Normalized();
    return true;
}

bool TryIntersectEditorGizmoRayPlane(
    const math::Vec3& rayOrigin,
    const math::Vec3& rayDirection,
    const math::Vec3& planePoint,
    const math::Vec3& planeNormal,
    math::Vec3& outIntersection)
{
    const math::Vec3 direction = rayDirection.Normalized();
    const math::Vec3 normal = planeNormal.Normalized();
    if (direction.LengthSq() <= math::Epsilon * math::Epsilon ||
        normal.LengthSq() <= math::Epsilon * math::Epsilon)
    {
        return false;
    }
    const float denominator = math::Vec3::Dot(direction, normal);
    if (std::fabs(denominator) <= 0.0001f)
    {
        return false;
    }
    const float distance =
        math::Vec3::Dot(planePoint - rayOrigin, normal) / denominator;
    if (distance < 0.0f)
    {
        return false;
    }
    outIntersection = rayOrigin + direction * distance;
    return true;
}

math::Vec3 ApplyEditorGizmoRotationDelta(
    const math::Vec3& startRotation,
    EditorGizmoAxis axis,
    EditorGizmoSpace space,
    float deltaRadians)
{
    const math::Vec3 canonicalAxis = GetCanonicalAxis(axis);
    if (canonicalAxis.LengthSq() <= math::Epsilon * math::Epsilon)
    {
        return startRotation;
    }

    const math::Quat startOrientation = math::Quat::FromEulerXYZ(
        startRotation.x, startRotation.y, startRotation.z);
    const math::Quat deltaRotation = math::Quat::FromAxisAngle(
        canonicalAxis, deltaRadians);

    // Localは start * delta、Worldは delta * start とし、TransformのEuler XYZ規約へ戻します。
    const math::Quat result = space == EditorGizmoSpace::Local
        ? startOrientation * deltaRotation
        : deltaRotation * startOrientation;
    return result.Normalized().ToEulerXYZ();
}

float ApplyEditorGizmoMinimumScale(float value, float originalValue)
{
    constexpr float minimumAbsoluteScale = 0.01f;
    const float sign = originalValue < 0.0f ? -1.0f : 1.0f;
    if (sign > 0.0f && value < minimumAbsoluteScale)
    {
        return minimumAbsoluteScale;
    }
    if (sign < 0.0f && value > -minimumAbsoluteScale)
    {
        return -minimumAbsoluteScale;
    }
    return value;
}

EditorGizmoDrawData EditorTransformGizmoController::BuildDrawData(
    Entity selectedEntity,
    const EditorCamera& camera,
    const EditorGizmoViewportRect& viewport,
    const math::Vec2& pointerPosition)
{
    EditorGizmoDrawData result;
    if (m_ActiveAxis != EditorGizmoAxis::None &&
        (IsEditableGizmoEntity(selectedEntity) == false ||
            selectedEntity.GetValue() != m_EntityValue))
    {
        // Scene交換や選択変更後のEntityを逆参照しないため、Handle不一致時は状態だけ破棄します。
        Reset();
    }

    EditorGizmoGeometry geometry;
    const EditorGizmoOperation operation = m_ActiveAxis != EditorGizmoAxis::None
        ? m_ActiveOperation : GetEditorGizmoOperation();
    const EditorGizmoSpace space = m_ActiveAxis != EditorGizmoAxis::None
        ? m_ActiveSpace : GetEditorGizmoSpace();
    if (BuildGizmoGeometry(
        selectedEntity, camera, viewport, operation, space, geometry) == false)
    {
        return result;
    }

    result.ActiveAxis = m_ActiveAxis;
    result.HoveredAxis = m_ActiveAxis == EditorGizmoAxis::None
        ? HitTestGizmo(geometry, operation, pointerPosition)
        : EditorGizmoAxis::None;

    if (operation != EditorGizmoOperation::Rotate)
    {
        for (const EditorGizmoAxisGeometry& axis : geometry.Axes)
        {
            if (axis.Visible == false)
            {
                continue;
            }
            const bool highlighted = axis.Axis == result.HoveredAxis ||
                axis.Axis == result.ActiveAxis;
            result.Primitives.push_back(EditorGizmoPrimitive{
                EditorGizmoPrimitiveType::Line,
                geometry.OriginScreen,
                axis.EndScreen,
                GetAxisColor(axis.Axis, highlighted),
                highlighted == true ? 5.0f : 3.0f,
                0.0f });
            result.Primitives.push_back(EditorGizmoPrimitive{
                operation == EditorGizmoOperation::Scale
                    ? EditorGizmoPrimitiveType::Square
                    : EditorGizmoPrimitiveType::Circle,
                axis.EndScreen,
                axis.EndScreen,
                GetAxisColor(axis.Axis, highlighted),
                0.0f,
                highlighted == true ? 6.0f : 5.0f });
        }
        return result;
    }

    for (const EditorGizmoRingGeometry& ring : geometry.Rings)
    {
        const bool highlighted = ring.Axis == result.HoveredAxis ||
            ring.Axis == result.ActiveAxis;
        for (std::size_t index = 0u; index < ring.SegmentVisible.size(); ++index)
        {
            if (ring.SegmentVisible[index] == false)
            {
                continue;
            }
            result.Primitives.push_back(EditorGizmoPrimitive{
                EditorGizmoPrimitiveType::Line,
                ring.Points[index],
                ring.Points[index + 1u],
                GetAxisColor(ring.Axis, highlighted),
                highlighted == true ? 4.0f : 2.5f,
                0.0f });
        }
    }
    return result;
}

bool EditorTransformGizmoController::HandlePointerEvent(
    Entity selectedEntity,
    const EditorCamera& camera,
    const EditorGizmoPointerEvent& event)
{
    const bool wasActive = IsActive();
    if (event.Type == EditorGizmoPointerEventType::Cancel)
    {
        if (wasActive == true && IsEditableGizmoEntity(selectedEntity) == true &&
            selectedEntity.GetValue() == m_EntityValue)
        {
            TransformComponent& transform =
                selectedEntity.GetComponent<TransformComponent>();
            transform.Position = m_DragStartPosition;
            transform.Rotation = m_DragStartRotation;
            transform.Scale = m_DragStartScale;
        }
        Reset();
        return wasActive;
    }

    if (IsEditableGizmoEntity(selectedEntity) == false)
    {
        Reset();
        return wasActive;
    }
    if (wasActive == true && selectedEntity.GetValue() != m_EntityValue)
    {
        Reset();
        return false;
    }

    TransformComponent& transform = selectedEntity.GetComponent<TransformComponent>();
    if (event.Type == EditorGizmoPointerEventType::Down)
    {
        if (wasActive == true)
        {
            return true;
        }
        EditorGizmoGeometry geometry;
        const EditorGizmoOperation operation = GetEditorGizmoOperation();
        const EditorGizmoSpace space = GetEditorGizmoSpace();
        if (BuildGizmoGeometry(
            selectedEntity, camera, event.Viewport, operation, space, geometry) == false)
        {
            return false;
        }
        const EditorGizmoAxis hitAxis =
            HitTestGizmo(geometry, operation, event.Position);
        if (hitAxis == EditorGizmoAxis::None)
        {
            return false;
        }

        m_ActiveAxis = hitAxis;
        m_ActiveOperation = operation;
        m_ActiveSpace = space;
        m_EntityValue = selectedEntity.GetValue();
        m_DragStartMouse = event.Position;
        m_DragStartPosition = transform.Position;
        m_DragStartRotation = transform.Rotation;
        m_DragStartScale = transform.Scale;
        m_OriginScreen = geometry.OriginScreen;

        if (operation == EditorGizmoOperation::Rotate)
        {
            const math::Vec2 fromCenter = event.Position - geometry.OriginScreen;
            if (fromCenter.LengthSq() <= math::Epsilon * math::Epsilon)
            {
                Reset();
                return false;
            }
            m_DragStartDirection = fromCenter.Normalized();
            return true;
        }

        for (const EditorGizmoAxisGeometry& axis : geometry.Axes)
        {
            if (axis.Axis != hitAxis || axis.Visible == false)
            {
                continue;
            }
            m_ScreenAxisDirection = axis.ScreenVector / axis.ScreenLength;
            m_WorldAxisDirection = axis.WorldDirection;
            m_PixelsPerUnit = operation == EditorGizmoOperation::Scale
                ? axis.ScreenLength
                : axis.ScreenLength / geometry.WorldLength;

            if (operation == EditorGizmoOperation::Translate)
            {
                m_DragPlanePoint = transform.Position;
                const math::Vec3 viewDirection =
                    (transform.Position - camera.GetPosition()).Normalized();
                math::Vec3 rayOrigin{};
                math::Vec3 rayDirection{};
                m_UseTranslateDragPlane =
                    TryBuildEditorGizmoDragPlaneNormal(
                        m_WorldAxisDirection,
                        viewDirection,
                        m_DragPlaneNormal) == true &&
                    TryBuildGizmoPointerRay(
                        camera,
                        event.Viewport,
                        event.Position,
                        rayOrigin,
                        rayDirection) == true &&
                    TryIntersectEditorGizmoRayPlane(
                        rayOrigin,
                        rayDirection,
                        m_DragPlanePoint,
                        m_DragPlaneNormal,
                        m_DragStartIntersection) == true;
            }
            return true;
        }
        Reset();
        return false;
    }

    if (IsActive() == false)
    {
        return false;
    }

    // Release位置にも最後のdeltaを適用してから履歴へ確定します。
    if (m_ActiveOperation == EditorGizmoOperation::Rotate)
    {
        const math::Vec2 fromCenter = event.Position - m_OriginScreen;
        if (fromCenter.LengthSq() > math::Epsilon * math::Epsilon)
        {
            const math::Vec2 direction = fromCenter.Normalized();
            const float cross = math::Vec2::Cross(m_DragStartDirection, direction);
            const float dot = math::Vec2::Dot(m_DragStartDirection, direction);
            float delta = -std::atan2(cross, dot);
            if (event.Snap == true)
            {
                delta = ApplyEditorGizmoSnap(
                    delta, GetEditorGizmoSnapSettings().RotateStepRadians);
            }
            transform.Rotation = ApplyEditorGizmoRotationDelta(
                m_DragStartRotation,
                m_ActiveAxis,
                m_ActiveSpace,
                delta);
        }
    }
    else if (m_ActiveOperation == EditorGizmoOperation::Translate)
    {
        float delta = 0.0f;
        bool hasDelta = false;
        if (m_UseTranslateDragPlane == true)
        {
            math::Vec3 rayOrigin{};
            math::Vec3 rayDirection{};
            math::Vec3 intersection{};
            if (TryBuildGizmoPointerRay(
                    camera,
                    event.Viewport,
                    event.Position,
                    rayOrigin,
                    rayDirection) == true &&
                TryIntersectEditorGizmoRayPlane(
                    rayOrigin,
                    rayDirection,
                    m_DragPlanePoint,
                    m_DragPlaneNormal,
                    intersection) == true)
            {
                delta = math::Vec3::Dot(
                    intersection - m_DragStartIntersection,
                    m_WorldAxisDirection);
                hasDelta = true;
            }
        }
        else if (m_PixelsPerUnit > math::Epsilon)
        {
            // Cameraと軸がほぼ平行でDrag Planeが退化する場合だけ、従来のScreen投影へfallbackします。
            const float projectedPixels = math::Vec2::Dot(
                event.Position - m_DragStartMouse,
                m_ScreenAxisDirection);
            delta = projectedPixels / m_PixelsPerUnit;
            hasDelta = true;
        }

        if (hasDelta == true)
        {
            if (event.Snap == true)
            {
                delta = ApplyEditorGizmoSnap(
                    delta, GetEditorGizmoSnapSettings().TranslateStep);
            }
            transform.Position =
                m_DragStartPosition + m_WorldAxisDirection * delta;
        }
    }
    else if (m_PixelsPerUnit > math::Epsilon)
    {
        const float projectedPixels = math::Vec2::Dot(
            event.Position - m_DragStartMouse, m_ScreenAxisDirection);
        float delta = projectedPixels / m_PixelsPerUnit;
        if (event.Snap == true)
        {
            delta = ApplyEditorGizmoSnap(
                delta, GetEditorGizmoSnapSettings().ScaleStep);
        }

        transform.Scale = m_DragStartScale;
        const float startValue = GetScaleAxisValue(
            m_DragStartScale, m_ActiveAxis);
        SetScaleAxisValue(
            transform.Scale,
            m_ActiveAxis,
            startValue + delta,
            m_DragStartScale);
    }

    if (event.Type == EditorGizmoPointerEventType::Up)
    {
        TransformComponent beforeTransform = transform;
        beforeTransform.Position = m_DragStartPosition;
        beforeTransform.Rotation = m_DragStartRotation;
        beforeTransform.Scale = m_DragStartScale;
        RecordAlreadyExecutedEditorCommand(
            std::make_unique<TransformCommand>(
                selectedEntity, beforeTransform, transform));
        Reset();
    }
    return true;
}

bool EditorTransformGizmoController::IsActive() const
{
    return m_ActiveAxis != EditorGizmoAxis::None;
}

void EditorTransformGizmoController::Reset()
{
    m_ActiveAxis = EditorGizmoAxis::None;
    m_ActiveOperation = EditorGizmoOperation::Translate;
    m_ActiveSpace = EditorGizmoSpace::World;
    m_EntityValue = 0u;
    m_DragStartMouse = math::Vec2{};
    m_DragStartDirection = math::Vec2{};
    m_ScreenAxisDirection = math::Vec2{};
    m_OriginScreen = math::Vec2{};
    m_DragStartPosition = math::Vec3{};
    m_DragStartRotation = math::Vec3{};
    m_DragStartScale = math::Vec3(1.0f, 1.0f, 1.0f);
    m_WorldAxisDirection = math::Vec3{};
    m_DragPlanePoint = math::Vec3{};
    m_DragPlaneNormal = math::Vec3{};
    m_DragStartIntersection = math::Vec3{};
    m_PixelsPerUnit = 0.0f;
    m_UseTranslateDragPlane = false;
}

bool RenderEditorTransformGizmo(
    Entity selectedEntity,
    const EditorCamera& camera,
    float viewportMinX,
    float viewportMinY,
    float viewportMaxX,
    float viewportMaxY)
{
    switch (s_Operation)
    {
    case EditorGizmoOperation::Translate:
        // 非アクティブ側へInvalid Entityを渡して内部Drag状態を確実に解除します。
        RenderRotateGizmo(Entity{}, camera, viewportMinX, viewportMinY, viewportMaxX, viewportMaxY);
        RenderScaleGizmo(Entity{}, camera, viewportMinX, viewportMinY, viewportMaxX, viewportMaxY);

        return RenderTranslateGizmo(
            selectedEntity,
            camera,
            viewportMinX,
            viewportMinY,
            viewportMaxX,
            viewportMaxY);

    case EditorGizmoOperation::Rotate:
        RenderTranslateGizmo(Entity{}, camera, viewportMinX, viewportMinY, viewportMaxX, viewportMaxY);
        RenderScaleGizmo(Entity{}, camera, viewportMinX, viewportMinY, viewportMaxX, viewportMaxY);

        return RenderRotateGizmo(
            selectedEntity,
            camera,
            viewportMinX,
            viewportMinY,
            viewportMaxX,
            viewportMaxY);

    case EditorGizmoOperation::Scale:
        RenderTranslateGizmo(Entity{}, camera, viewportMinX, viewportMinY, viewportMaxX, viewportMaxY);
        RenderRotateGizmo(Entity{}, camera, viewportMinX, viewportMinY, viewportMaxX, viewportMaxY);

        return RenderScaleGizmo(
            selectedEntity,
            camera,
            viewportMinX,
            viewportMinY,
            viewportMaxX,
            viewportMaxY);

    default:
        return false;
    }
}

} // namespace Raven
