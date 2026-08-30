#pragma once

#include "Raven/Scene/Entity.h"

namespace Raven
{

class EditorCamera;

// ============================================================================
// RenderRotateGizmo
// ============================================================================
// Scene View上へX / Y / Z軸を法線とする回転Ringを描画し、Mouse Dragを
// 選択EntityのTransformComponent::Rotationへ反映します。
//
// Worldでは固定World軸を法線とするRing、LocalではTransformComponent::Rotationから
// Quaternionで回転したEntity自身のLocal軸へRingを追従させます。
// 実際の回転適用もQuaternion合成を使い、Worldは delta * current、Localは current * delta と
// 乗算順序を分けた後、Scene互換形式であるEuler XYZ(radian)へ戻します。
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
