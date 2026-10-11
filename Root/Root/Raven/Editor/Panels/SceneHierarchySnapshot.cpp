#include "Raven/Editor/Panels/SceneHierarchySnapshot.h"

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Raven
{
namespace
{
bool HasValidParentChain(
    EntityHandle handle,
    const std::unordered_map<std::uint64_t, EntityHandle>& parents)
{
    std::unordered_set<std::uint64_t> visited;
    EntityHandle current = handle;
    while (static_cast<bool>(current) == true)
    {
        if (visited.insert(current.Value()).second == false)
        {
            return false;
        }

        const auto iterator = parents.find(current.Value());
        if (iterator == parents.end())
        {
            return true;
        }
        current = iterator->second;
    }
    return true;
}
} // namespace

SceneHierarchySnapshot BuildSceneHierarchySnapshot(
    const SceneHierarchySnapshotInput& input)
{
    SceneHierarchySnapshot result{};
    result.SceneAvailable = input.SceneAvailable;
    if (input.SceneAvailable == false)
    {
        return result;
    }

    std::unordered_set<std::uint64_t> handles;
    handles.reserve(input.Nodes.size());
    result.Nodes.reserve(input.Nodes.size());
    for (const SceneHierarchyNodeSnapshot& node : input.Nodes)
    {
        // 無効Handleと重複HandleはTreeの安定IDとして利用できないため除外します。
        // 同名EntityはHandleが異なる限り保持します。
        if (static_cast<bool>(node.Handle) == false ||
            handles.insert(node.Handle.Value()).second == false)
        {
            continue;
        }
        SceneHierarchyNodeSnapshot copy = node;
        if (copy.DisplayName.empty() == true)
        {
            copy.DisplayName = "Entity";
        }
        result.Nodes.push_back(std::move(copy));
    }

    std::unordered_map<std::uint64_t, EntityHandle> parents;
    parents.reserve(result.Nodes.size());
    for (const SceneHierarchyNodeSnapshot& node : result.Nodes)
    {
        EntityHandle parent = node.Parent;
        if (static_cast<bool>(parent) == false ||
            handles.find(parent.Value()) == handles.end() ||
            parent == node.Handle)
        {
            parent = InvalidEntityHandle;
        }
        parents.emplace(node.Handle.Value(), parent);
    }

    for (SceneHierarchyNodeSnapshot& node : result.Nodes)
    {
        node.Parent = parents[node.Handle.Value()];
        // 破損データで循環してもTree構築を再帰させません。循環へ到達するNodeは
        // 安全なRootへ昇格し、表示と選択を継続できるようにします。
        if (HasValidParentChain(node.Handle, parents) == false)
        {
            node.Parent = InvalidEntityHandle;
        }
    }

    if (static_cast<bool>(input.Selected) == true &&
        handles.find(input.Selected.Value()) != handles.end())
    {
        result.Selected = input.Selected;
    }
    return result;
}

SceneHierarchySnapshot CaptureSceneHierarchySnapshot(
    Scene* scene, Entity selectedEntity)
{
    SceneHierarchySnapshotInput input{};
    if (scene == nullptr)
    {
        return BuildSceneHierarchySnapshot(input);
    }

    input.SceneAvailable = true;
    if (static_cast<bool>(selectedEntity) == true &&
        selectedEntity.GetScene() == scene &&
        scene->IsEntityAlive(selectedEntity) == true)
    {
        input.Selected = selectedEntity.GetHandle();
    }

    // 現在のECSには親子Relationship Componentが無いため、全EntityをRootとして取得します。
    // 親子関係の正規データが追加された際はParentだけを設定し、Panel/UI契約は維持します。
    for (auto [entity, tag] : scene->View<TagComponent>())
    {
        input.Nodes.push_back(SceneHierarchyNodeSnapshot{
            entity.GetHandle(), InvalidEntityHandle, tag.Tag });
    }
    return BuildSceneHierarchySnapshot(input);
}

} // namespace Raven
