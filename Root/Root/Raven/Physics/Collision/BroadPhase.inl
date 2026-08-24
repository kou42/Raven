#pragma once

#include "Raven/Scene/Scene.h"

namespace Raven::ph
{

template <class PairContainer>
void BroadPhase::ComputePairs(Scene& scene, PairContainer& outPairs)
{
    Synchronize(scene);
    outPairs.clear();

    // Pairを順序正規化して保持し、(A,B) / (B,A) の重複通知を防ぎます。
    // emitted自体は現段階では通常heapを使用し、Pair列のFrameAllocator移行効果を
    // 先に分離計測できるようにしています。
    std::unordered_set<CollisionIgnorePairKey, CollisionIgnorePairKeyHasher> emitted;

    for (const auto& [entityValue, proxyId] : m_Proxies)
    {
        const Entity entity(EntityHandle::FromValue(entityValue), &scene);
        if (scene.IsEntityAlive(entity) == false)
        {
            continue;
        }

        // 自身のFat AABBで木を問合せ、重なり候補だけを抽出します。
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

                // ============================================================
                // Collision Ignore Pair Filter
                // ============================================================
                // Joint接続されたRagdoll BodyなどはAABBが重なりやすいですが、
                // Narrow Phaseへ渡す前にここで除外します。
                // Tree Proxy自体は維持するため、RayCast / QueryAABBでは通常通り検索できます。
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

    // 重要:
    // Debug RendererはBroad Phaseを再実行せず、このSnapshotを表示します。
    // FrameAllocator由来のoutPairsはResetFrame()後に無効になるため、診断用Snapshotだけは
    // 通常vectorへコピーしてPhysics Step後も安全に参照できる寿命を確保します。
    m_LastPairs.assign(outPairs.begin(), outPairs.end());
}

template <class Callback>
void BroadPhase::QueryAABB(Scene& scene, const AABB& queryBounds, Callback&& callback)
{
    // Query前にScene同期を必ず実行し、古いProxy状態での取りこぼしを防ぎます。
    Synchronize(scene);
    m_Tree.Query(queryBounds,
        [&](Entity entity, uint32_t proxyId) -> bool
        {
            if (scene.IsEntityAlive(entity) == false)
            {
                // 破棄済みEntityは結果に含めず探索だけ継続します。
                return true;
            }
            return callback(entity, proxyId);
        });
}

template <class Callback>
void BroadPhase::RayCast(
    Scene& scene,
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    Callback&& callback)
{
    // RayCastでも同期を先に行い、AABB Treeの状態とScene実体を一致させます。
    Synchronize(scene);
    m_Tree.RayCast(
        origin,
        direction,
        maxFraction,
        [&](Entity entity,
            uint32_t proxyId,
            float fraction,
            const math::Vec3& normal,
            float currentMaxFraction) -> float
        {
            if (scene.IsEntityAlive(entity) == false)
            {
                // 無効Entityは無視し、現在の最短距離制約をそのまま維持します。
                return currentMaxFraction;
            }

            // callbackの戻り値で探索上限距離を更新し、早期枝刈りに利用します。
            return callback(
                entity,
                proxyId,
                fraction,
                normal,
                currentMaxFraction);
        });
}

} // namespace Raven::ph