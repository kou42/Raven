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

// Scene依存のない組み立て入力です。Runtimeの親子関係はSceneGraphが検証し、
// UIは外部入力やSnapshotの破損にも備えて循環・孤児を別途正規化します。
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
