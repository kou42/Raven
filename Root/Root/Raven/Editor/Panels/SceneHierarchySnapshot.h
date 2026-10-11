#pragma once

#include "Raven/Scene/Entity.h"

#include <string>
#include <vector>

namespace Raven
{

class Scene;

// Scene Hierarchyが1frameで表示するEntity情報です。
// RuntimeのComponent参照やScene pointerを保持せず、Entityの識別に必要な
// Index + Generationと表示名だけを値として保持します。
struct SceneHierarchyNodeSnapshot
{
    EntityHandle Handle = InvalidEntityHandle;
    EntityHandle Parent = InvalidEntityHandle;
    std::string DisplayName;
};

struct SceneHierarchySnapshot
{
    bool SceneAvailable = false;
    EntityHandle Selected = InvalidEntityHandle;
    std::vector<SceneHierarchyNodeSnapshot> Nodes;
};

// Scene依存のない組み立て入力です。将来Relationship Componentが追加された際も、
// UI側の契約を変えずParent Handleへ写せるよう親子関係を明示します。
struct SceneHierarchySnapshotInput
{
    bool SceneAvailable = false;
    EntityHandle Selected = InvalidEntityHandle;
    std::vector<SceneHierarchyNodeSnapshot> Nodes;
};

SceneHierarchySnapshot BuildSceneHierarchySnapshot(
    const SceneHierarchySnapshotInput& input);
SceneHierarchySnapshot CaptureSceneHierarchySnapshot(
    Scene* scene, Entity selectedEntity);

} // namespace Raven
