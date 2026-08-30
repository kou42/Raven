#include "Raven/Editor/EditorGizmo.h"

#include "Raven/Editor/EditorRotateGizmo.h"
#include "Raven/Editor/EditorScaleGizmo.h"
#include "Raven/Editor/EditorTranslateGizmo.h"

namespace Raven
{
namespace
{

// Editor全体で現在選択中のGizmo操作を保持します。
// EditorLayerは操作モードの値を保持せず、UIからSet/Get APIだけを利用します。
EditorGizmoOperation s_Operation = EditorGizmoOperation::Translate;

} // namespace

EditorGizmoOperation GetEditorGizmoOperation()
{
    return s_Operation;
}

void SetEditorGizmoOperation(EditorGizmoOperation operation)
{
    s_Operation = operation;
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
