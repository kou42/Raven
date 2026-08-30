#pragma once

#include "Raven/Scene/Entity.h"

namespace Raven
{

class EditorCamera;

// ============================================================================
// RenderTranslateGizmo
// ============================================================================
// Scene View上へWorld X / Y / Z軸のTranslate Gizmoを描画し、Mouse Dragを
// 選択EntityのTransformComponent::Positionへ反映します。
//
// 現在は既存EditorLayerとの互換性を保つScene View Gizmo入口も兼ねています。
// W=Translate / E=Rotateの操作モードを確認し、Rotate選択中はEditorRotateGizmoへ委譲します。
// 将来EditorLayer側をRenderEditorTransformGizmo()へ移行した後は、再びTranslate専用入口へ
// 戻せるよう、実際の操作モード状態はEditorGizmo側へ分離しています。
//
// この関数はDear ImGuiを利用しますが、ImGui型を公開シグネチャへ出さないことで
// EditorLayer.hやRenderer共通層へImGui依存を広げません。
//
// 戻り値:
//   true  : Gizmoが現在のMouse入力を消費している
//   false : Scene View Picking等がMouse入力を処理してよい
bool RenderTranslateGizmo(
    Entity selectedEntity,
    const EditorCamera& camera,
    float viewportMinX,
    float viewportMinY,
    float viewportMaxX,
    float viewportMaxY);

} // namespace Raven
