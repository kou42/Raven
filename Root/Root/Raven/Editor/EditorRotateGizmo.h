#pragma once

#include "Raven/Scene/Entity.h"

namespace Raven
{

class EditorCamera;

// ============================================================================
// RenderRotateGizmo
// ============================================================================
// Scene View上へWorld X / Y / Z軸を法線とする回転Ringを描画し、Mouse Dragを
// 選択EntityのTransformComponent::Rotationへ反映します。
//
// TransformComponent::Rotationは現在Euler角(radian)を保持しているため、World軸Ringを
// X/Y/Zの各Euler成分へ対応させます。将来Local Gizmoへ拡張する際は、現在Rotationから
// Local基底を作り、Ring自体をEntity姿勢へ追従させる構造へ拡張できます。
//
// 戻り値:
//   true  : Gizmoが現在のMouse入力を消費している
//   false : Scene View Picking等がMouse入力を処理してよい
bool RenderRotateGizmo(
    Entity selectedEntity,
    const EditorCamera& camera,
    float viewportMinX,
    float viewportMinY,
    float viewportMaxX,
    float viewportMaxY);

} // namespace Raven
