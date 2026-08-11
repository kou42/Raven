#pragma once

#include "Raven/Scene/Entity.h"

namespace Raven
{

// ============================================================================
// InspectorPanel
// ============================================================================
// Scene Hierarchyで選択されたEntityのComponentを表示・編集するEditor Panelです。
//
// このPanelはEntityの選択状態を所有しません。
// EditorLayerが保持している共通選択Entityを受け取り、そのEntityが持つComponentだけを
// 編集することで、Hierarchy / Inspector / 将来のScene View / Gizmoで同じ選択状態を共有します。
//
// 初期実装ではEditor操作の中心になる以下を対象にします。
//   - TagComponent
//   - TransformComponent
//   - RigidBodyComponent
//   - ColliderComponent
//
// Physics Componentについては、Mass変更時にInverseMassも同期する必要があるなど、単純に
// メンバを書き換えるだけでは不変条件を壊す項目があります。そのためInspectorからも
// SetMass() / SetBodyType()等のComponent公開APIを経由して更新します。
class InspectorPanel
{
public:
    void OnImGuiRender(Entity selectedEntity);
};

} // namespace Raven
