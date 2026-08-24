#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/DynamicAABBTree.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{
class Scene;
}

namespace Raven::ph
{

struct BroadPhasePair
{
    Entity A{};
    Entity B{};
};

struct CollisionIgnorePairKey
{
    uint64_t A = 0;
    uint64_t B = 0;

    bool operator==(const CollisionIgnorePairKey& rhs) const
    {
        return A == rhs.A && B == rhs.B;
    }
};

struct CollisionIgnorePairKeyHasher
{
    std::size_t operator()(const CollisionIgnorePairKey& key) const noexcept
    {
        std::size_t seed = std::hash<uint64_t>{}(key.A);
        seed ^= std::hash<uint64_t>{}(key.B)
            + static_cast<std::size_t>(0x9e3779b97f4a7c15ull)
            + (seed << 6) + (seed >> 2);
        return seed;
    }
};

class BroadPhase
{
public:
    // 出力Containerは clear() / push_back() / begin() / end() を満たせばよく、
    // std::vectorのAllocator型には依存しません。
    // これによりPhysicsの一時Pair列だけをFrameAllocatorへ段階的に移行できます。
    template <class PairContainer>
    void ComputePairs(Scene& scene, PairContainer& outPairs)
    {
        Synchronize(scene);
        outPairs.clear();

        // Pair重複除去用Setは現段階では永続heapを使用します。
        // 次段階でProfiler結果を見ながら一時Allocator化する対象として分離しておきます。
        std::unordered_set<CollisionIgnorePairKey, CollisionIgnorePairKeyHasher> emitted;

        for (const auto& [entityValue, proxyId] : m_Proxies)
        {
            const Entity entity(EntityHandle::FromValue(entityValue), &scene);
            if (scene.IsEntityAlive(entity) == false)
            {
                continue;
            }

            const AABB queryBounds = m_Tree.GetFatAABB(proxyId);
            m_Tree.Query(queryBounds,
                [&](Entity other, uint32_t otherProxy) -> bool
                {
                    if (otherProxy == proxyId || scene.IsEntityAlive(other) == false)
                    {
                        return true;
                    }

                    Entity a = entity;
                    Entity b = other;
                    if (a.GetValue() > b.GetValue())
                    {
                        std::swap(a, b);
                    }

                    const CollisionIgnorePairKey key = MakeIgnorePairKey(a, b);
                    if (m_IgnorePairs.find(key) != m_IgnorePairs.end())
                    {
                        return true;
                    }

                    if (emitted.insert(key).second)
                    {
                        outPairs.push_back(BroadPhasePair{ a, b });
                    }
                    return true;
                });
        }

        // m_LastPairsはDebug OverlayがPhysics Step後にも参照する永続Snapshotです。
        // FrameAllocatorの寿命を越えて保持してはいけないため、ここだけ通常vectorへコピーします。
        m_LastPairs.assign(outPairs.begin(), outPairs.end());
    }

    void AddIgnorePair(Entity a, Entity b);
    void RemoveIgnorePair(Entity a, Entity b);
    void RemoveIgnorePairsForEntity(Entity entity);
    bool IsPairIgnored(Entity a, Entity b) const;
    void ClearIgnorePairs();

    const std::vector<BroadPhasePair>& GetLastPairs() const { return m_LastPairs; }

    template <class Callback>
    void QueryAABB(Scene& scene, const AABB& queryBounds, Callback&& callback);

    template <class Callback>
    void RayCast(
        Scene& scene,
        const math::Vec3& origin,
        const math::Vec3& direction,
        float maxFraction,
        Callback&& callback);

    DynamicAABBTree& GetTree() { return m_Tree; }
    const DynamicAABBTree& GetTree() const { return m_Tree; }

private:
    static CollisionIgnorePairKey MakeIgnorePairKey(Entity a, Entity b);
    void Synchronize(Scene& scene);

private:
    DynamicAABBTree m_Tree;
    std::unordered_map<uint64_t, uint32_t> m_Proxies;
    std::unordered_map<uint64_t, math::Vec3> m_PreviousCenters;
    std::unordered_set<CollisionIgnorePairKey, CollisionIgnorePairKeyHasher> m_IgnorePairs;
    std::vector<BroadPhasePair> m_LastPairs;
};

} // namespace Raven::ph
