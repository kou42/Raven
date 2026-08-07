#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/DynamicAABBTree.h"
#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{

struct BroadPhasePair
{
    Entity A{};
    Entity B{};
};

// ============================================================================
// BroadPhase
// ============================================================================
// Dynamic AABB Treeを保持し、Scene内Colliderの追加・移動・削除をProxyへ同期します。
//
// 以前のSweep-and-PruneはComputePairs()ごとに全Colliderを収集・sortしていました。
// Dynamic TreeではFat AABB内の微小移動ならTree構造を変更せず、必要なLeafだけを
// 再挿入します。
class BroadPhase
{
public:
    void ComputePairs(Scene& scene, std::vector<BroadPhasePair>& outPairs)
    {
        Synchronize(scene);
        outPairs.clear();

        // 同一Pairを複数Leafから発見しても1回だけ出力するためのキーです。
        // EntityHandle全体を使うので、Entity Index再利用時のGenerationも区別します。
        struct PairKey
        {
            uint64_t A = 0;
            uint64_t B = 0;

            bool operator==(const PairKey& rhs) const
            {
                return A == rhs.A && B == rhs.B;
            }
        };

        struct PairHasher
        {
            std::size_t operator()(const PairKey& key) const noexcept
            {
                std::size_t seed = std::hash<uint64_t>{}(key.A);
                seed ^= std::hash<uint64_t>{}(key.B)
                    + static_cast<std::size_t>(0x9e3779b97f4a7c15ull)
                    + (seed << 6) + (seed >> 2);
                return seed;
            }
        };

        std::unordered_set<PairKey, PairHasher> emitted;

        for (const auto& [entityValue, proxyId] : m_Proxies)
        {
            const Entity entity(EntityHandle::FromValue(entityValue), &scene);
            if (!scene.IsEntityAlive(entity))
            {
                continue;
            }

            const AABB queryBounds = m_Tree.GetFatAABB(proxyId);
            m_Tree.Query(queryBounds,
                [&](Entity other, uint32_t otherProxy) -> bool
                {
                    if (otherProxy == proxyId || !scene.IsEntityAlive(other))
                    {
                        return true;
                    }

                    Entity a = entity;
                    Entity b = other;
                    if (a.GetValue() > b.GetValue())
                    {
                        std::swap(a, b);
                    }

                    const PairKey key{ a.GetValue(), b.GetValue() };
                    if (emitted.insert(key).second)
                    {
                        outPairs.push_back(BroadPhasePair{ a, b });
                    }
                    return true;
                });
        }
    }

    DynamicAABBTree& GetTree() { return m_Tree; }
    const DynamicAABBTree& GetTree() const { return m_Tree; }

private:
    void Synchronize(Scene& scene)
    {
        std::unordered_set<uint64_t> seen;

        for (auto [entity, transform, collider]
            : scene.View<TransformComponent, ColliderComponent>())
        {
            AABB tightBounds{};
            if (!ComputeColliderAABB(transform, collider, tightBounds))
            {
                continue;
            }

            const uint64_t entityValue = entity.GetValue();
            seen.insert(entityValue);

            const auto found = m_Proxies.find(entityValue);
            if (found == m_Proxies.end())
            {
                const uint32_t proxy = m_Tree.CreateProxy(tightBounds, entity);
                m_Proxies.emplace(entityValue, proxy);
                m_PreviousCenters[entityValue] = tightBounds.GetCenter();
                continue;
            }

            const math::Vec3 center = tightBounds.GetCenter();
            const math::Vec3 previousCenter = m_PreviousCenters[entityValue];
            const math::Vec3 displacement = center - previousCenter;

            m_Tree.MoveProxy(found->second, tightBounds, displacement);
            m_PreviousCenters[entityValue] = center;
        }

        // Sceneから削除されたEntity/ColliderのProxyをTreeから除去します。
        for (auto it = m_Proxies.begin(); it != m_Proxies.end(); )
        {
            if (seen.find(it->first) != seen.end())
            {
                ++it;
                continue;
            }

            m_Tree.DestroyProxy(it->second);
            m_PreviousCenters.erase(it->first);
            it = m_Proxies.erase(it);
        }
    }

private:
    DynamicAABBTree m_Tree;

    // EntityHandle::Value() -> Tree Proxy ID
    std::unordered_map<uint64_t, uint32_t> m_Proxies;

    // Fat AABBを移動方向へ予測拡張するため、前回中心を保持します。
    std::unordered_map<uint64_t, math::Vec3> m_PreviousCenters;
};

} // namespace Raven::ph
