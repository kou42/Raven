#include "Raven/Scene/SceneGraph.h"
#include "Raven/Scene/Scene.h"

#include <unordered_map>
#include <unordered_set>

namespace Raven
{
EntityHandle SceneGraph::GetAliveHandle(const Scene& scene, EntityIndex index)
{
    if (index == InvalidEntityIndex || index >= scene.m_EntitySlots.size())
    {
        return InvalidEntityHandle;
    }
    const auto& slot = scene.m_EntitySlots[index];
    return slot.Alive == true ? EntityHandle{index, slot.Generation} : InvalidEntityHandle;
}

EntityHandle SceneGraph::GetParent(const Scene& scene, EntityHandle child)
{
    if (scene.IsEntityAlive(child) == false)
    {
        return InvalidEntityHandle;
    }
    const auto* relation = scene.TryGetComponent<RelationshipComponent>(child.m_Index);
    if (relation == nullptr || scene.IsEntityAlive(relation->Parent) == false)
    {
        return InvalidEntityHandle;
    }
    return relation->Parent;
}

bool SceneGraph::CanReparent(const Scene& scene, EntityHandle child, EntityHandle parent)
{
    if (scene.IsEntityAlive(child) == false || child == parent)
    {
        return false;
    }
    if (parent.IsValid() == false)
    {
        return true;
    }
    if (scene.IsEntityAlive(parent) == false)
    {
        return false;
    }

    // Slot数を上限にすることで、公開Componentの直接編集で既に循環していても停止します。
    // 通常の親変更では祖先だけを辿り、動的確保や全Entityの走査を行いません。
    EntityHandle current = parent;
    for (std::size_t depth = 0; depth < scene.m_EntitySlots.size(); ++depth)
    {
        if (current == child)
        {
            return false;
        }
        const auto* relation = scene.TryGetComponent<RelationshipComponent>(current.m_Index);
        if (relation == nullptr || relation->Parent.IsValid() == false)
        {
            return true;
        }
        if (scene.IsEntityAlive(relation->Parent) == false)
        {
            return false;
        }
        current = relation->Parent;
    }
    return false;
}

bool SceneGraph::Reparent(Scene& scene, EntityHandle child, EntityHandle parent)
{
    if (CanReparent(scene, child, parent) == false)
    {
        return false;
    }
    auto* relation = scene.TryGetComponent<RelationshipComponent>(child.m_Index);
    if (relation == nullptr)
    {
        scene.AddComponent<RelationshipComponent>(child.m_Index,
            RelationshipComponent{parent.IsValid() == true ? parent : InvalidEntityHandle});
    }
    else
    {
        relation->Parent = parent.IsValid() == true ? parent : InvalidEntityHandle;
    }
    return true;
}

bool SceneGraph::Detach(Scene& scene, EntityHandle child)
{
    return Reparent(scene, child, InvalidEntityHandle);
}

std::vector<EntityHandle> SceneGraph::GetChildren(const Scene& scene, EntityHandle parent)
{
    std::vector<EntityHandle> result;
    if (scene.IsEntityAlive(parent) == false)
    {
        return result;
    }
    const auto* storage = scene.FindStorage<RelationshipComponent>();
    if (storage == nullptr)
    {
        return result;
    }
    for (std::size_t i = 0; i < storage->Size(); ++i)
    {
        const EntityHandle child = GetAliveHandle(scene, storage->GetEntityByIndex(i));
        if (child.IsValid() == true && child != parent && storage->GetByIndex(i).Parent == parent)
        {
            result.push_back(child);
        }
    }
    return result;
}

std::vector<EntityHandle> SceneGraph::GetAncestors(const Scene& scene, EntityHandle child)
{
    std::vector<EntityHandle> result;
    if (scene.IsEntityAlive(child) == false)
    {
        return result;
    }
    std::unordered_set<uint64_t> visited{child.Value()};
    EntityHandle current = GetParent(scene, child);
    while (current.IsValid() == true)
    {
        if (visited.insert(current.Value()).second == false)
        {
            return {};
        }
        result.push_back(current);
        current = GetParent(scene, current);
    }
    return result;
}

std::vector<EntityHandle> SceneGraph::GetDescendants(const Scene& scene, EntityHandle root)
{
    std::vector<EntityHandle> result;
    if (scene.IsEntityAlive(root) == false)
    {
        return result;
    }
    const auto* storage = scene.FindStorage<RelationshipComponent>();
    if (storage == nullptr)
    {
        return result;
    }

    // 子ごとの全Storage再走査（O(Entity数×Relationship数)）を避けます。
    // この呼び出し内だけの索引なので、Component直接編集/Undo後のキャッシュ失効は不要です。
    std::unordered_map<uint64_t, std::vector<EntityHandle>> children;
    for (std::size_t i = 0; i < storage->Size(); ++i)
    {
        const EntityHandle child = GetAliveHandle(scene, storage->GetEntityByIndex(i));
        const EntityHandle parent = storage->GetByIndex(i).Parent;
        if (child.IsValid() == true && child != parent && scene.IsEntityAlive(parent) == true)
        {
            children[parent.Value()].push_back(child);
        }
    }
    std::vector<EntityHandle> pending{root};
    std::unordered_set<uint64_t> visited{root.Value()};
    for (std::size_t i = 0; i < pending.size(); ++i)
    {
        const auto entry = children.find(pending[i].Value());
        if (entry == children.end())
        {
            continue;
        }
        for (EntityHandle child : entry->second)
        {
            if (visited.insert(child.Value()).second == true)
            {
                pending.push_back(child);
                result.push_back(child);
            }
        }
    }
    return result;
}

void SceneGraph::DetachChildren(Scene& scene, EntityHandle parent)
{
    if (scene.IsEntityAlive(parent) == false)
    {
        return;
    }
    auto* storage = scene.FindStorage<RelationshipComponent>();
    if (storage == nullptr)
    {
        return;
    }
    // 値だけを変更します。Component参照を保持したままStorageを構造変更しません。
    for (std::size_t i = 0; i < storage->Size(); ++i)
    {
        auto& relation = storage->GetByIndex(i);
        if (relation.Parent == parent)
        {
            relation.Parent = InvalidEntityHandle;
        }
    }
}
}
