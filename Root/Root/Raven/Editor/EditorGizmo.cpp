#include "Raven/Editor/EditorGizmo.h"

#include "Raven/Editor/EditorRotateGizmo.h"
#include "Raven/Editor/EditorScaleGizmo.h"
#include "Raven/Editor/EditorTranslateGizmo.h"
#include "Raven/Math/MathQuatanion.h"

#include <cmath>

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
