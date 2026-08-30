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
