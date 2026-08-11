#pragma once

#include "Raven/Scene/Entity.h"

namespace Raven
{
class Scene;

// ============================================================================
// SceneHierarchyPanel
// ============================================================================
// Active Scene内のEntity一覧を表示し、Editorの選択Entityを更新するPanelです。
//
// Hierarchy自身は選択状態を所有しません。選択EntityはEditorLayer側で保持し、
// Hierarchy / Inspector / Scene View / Gizmoが同じ選択状態を共有できるようにします。
//
// 現在Scene::CreateEntity()は全EntityへTagComponentを付与するため、Entity列挙には
// Scene::View<TagComponent>()を利用します。Scene内部のEntitySlotやComponentStorageを
// Editorへ直接公開せず、既存ECS APIの範囲内で一覧を構築します。
class SceneHierarchyPanel
{
public:
    void OnImGuiRender(Scene* scene, Entity& selectedEntity);
};

} // namespace Raven
