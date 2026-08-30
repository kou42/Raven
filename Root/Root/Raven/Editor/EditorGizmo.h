#pragma once

#include "Raven/Scene/Entity.h"

namespace Raven
{

class EditorCamera;

// ============================================================================
// EditorGizmoOperation
// ============================================================================
// Scene Viewで現在使用するTransform操作を表します。
// EditorLayerへTranslate/Rotate/Scaleそれぞれの内部状態を持ち込まず、Gizmo機能側で操作モードを
// 一元管理することで、今後Local-World / Snapを追加してもEditorLayerを肥大化させません。
enum class EditorGizmoOperation
{
    Translate = 0,
    Rotate,
    Scale
};

EditorGizmoOperation GetEditorGizmoOperation();
void SetEditorGizmoOperation(EditorGizmoOperation operation);

// ============================================================================
// RenderEditorTransformGizmo
// ============================================================================
// 現在選択されている操作モードに対応したGizmoだけをScene Viewへ描画します。
// 非アクティブ側のGizmoへInvalid Entityを渡してDrag状態を明示的にリセットするため、
// 操作途中でTranslate/Rotate/Scaleを切り替えても古いDrag状態が次回の操作へ持ち越されません。
//
// 戻り値:
//   true  : GizmoがMouse入力を消費している
//   false : Entity Picking等がMouse入力を処理してよい
bool RenderEditorTransformGizmo(
    Entity selectedEntity,
    const EditorCamera& camera,
    float viewportMinX,
    float viewportMinY,
    float viewportMaxX,
    float viewportMaxY);

} // namespace Raven
