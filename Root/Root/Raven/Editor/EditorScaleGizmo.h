#pragma once

#include "Raven/Scene/Entity.h"

namespace Raven
{

class EditorCamera;

// ============================================================================
// RenderScaleGizmo
// ============================================================================
// Scene View上へX / Y / ZのScale Handleを描画し、Mouse Dragを選択Entityの
// TransformComponent::Scaleへ反映します。
//
// 現段階ではTransformComponentのScale成分に対応した軸別Scaleを扱います。
// Scaleが0付近まで縮むとModel Matrixが退化して後続の逆行列・法線変換等を不安定にするため、
// 操作結果には最小絶対値を設けます。
//
// 戻り値:
//   true  : Gizmoが現在のMouse入力を消費している
//   false : Entity Picking等がMouse入力を処理してよい
bool RenderScaleGizmo(
    Entity selectedEntity,
    const EditorCamera& camera,
    float viewportMinX,
    float viewportMinY,
    float viewportMaxX,
    float viewportMaxY);

} // namespace Raven
