#pragma once

#include "Raven/Core/CPUProfiler.h"
#include "Raven/Core/Memory/STLAllocatorAdapter.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace detail
{
// ============================================================================
// Raven Allocator Detection
// ============================================================================
// ComputePairs()は通常のstd::vectorとFrameVectorの両方を受け取れるAPIを維持します。
// STLAllocatorAdapterを使うContainerだけBacking Allocatorを取得し、それ以外の標準Allocatorでは
// nullptrを返して従来どおり通常heapの一時Setへフォールバックします。
template <typename T>
Allocator* GetRavenAllocator(const STLAllocatorAdapter<T>& allocator)
{
    return allocator.GetAllocator();
}

template <typename AllocatorType>
Allocator* GetRavenAllocator(const AllocatorType& allocator)
{
    static_cast<void>(allocator);
    return nullptr;
}
} // namespace detail

template <class PairContainer>
void BroadPhase::ComputePairs(Scene& scene, PairContainer& outPairs)
{
    Synchronize(scene);
    outPairs.clear();

    // ========================================================================
    // Previous Pair Count Reserve
    // ========================================================================
    // BroadPhase Pair数は隣接Frameで急変しないことが多いため、直前StepのPair数を
    // 次Stepの初期容量として利用します。
    //
    // FrameAllocatorではvectorがcapacity拡張するたびに新しい領域をArenaから確保し、
    // 古い領域はResetFrame()まで残ります。そのためreserve()で最初から近い容量を確保すると、
    // AllocationCountだけでなくUsedBytes / PeakBytesの増加も抑えられます。
    const std::size_t previousPairCount = m_LastPairs.size();
    if (previousPairCount > 0)
    {
        outPairs.reserve(previousPairCount);
    }

    // ========================================================================
    // Pair Generation
    // ========================================================================
    // 重複除去SetのAllocatorだけを切り替えられるよう、実際のTree走査処理はlambdaへまとめます。
    // FrameVector経路ではemittedのbucket配列と各nodeもPhysics FrameAllocatorから確保されます。
    // 通常std::vector経路では従来どおり標準unordered_setを使用します。
    const auto generatePairs =
        [&](auto& emitted)
        {
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
        };

    // FrameVectorのSTLAllocatorAdapterからPhysics FrameAllocatorを取得します。
    // 前フレームPair数をreserveしておくことでbucket再構築を抑えます。
    Allocator* temporaryAllocator = detail::GetRavenAllocator(outPairs.get_allocator());
    std::size_t emittedCount = 0;
    std::size_t emittedBucketCount = 0;

    if (temporaryAllocator != nullptr)
    {
        using EmittedAllocator = STLAllocatorAdapter<CollisionIgnorePairKey>;
        using EmittedSet = std::unordered_set<
            CollisionIgnorePairKey,
            CollisionIgnorePairKeyHasher,
            std::equal_to<CollisionIgnorePairKey>,
            EmittedAllocator>;

        EmittedSet emitted(
            0,
            CollisionIgnorePairKeyHasher{},
            std::equal_to<CollisionIgnorePairKey>{},
            EmittedAllocator(*temporaryAllocator));

        if (previousPairCount > 0)
        {
            emitted.reserve(previousPairCount);
        }

        generatePairs(emitted);
        emittedCount = emitted.size();
        emittedBucketCount = emitted.bucket_count();
    }
    else
    {
        std::unordered_set<CollisionIgnorePairKey, CollisionIgnorePairKeyHasher> emitted;

        if (previousPairCount > 0)
        {
            emitted.reserve(previousPairCount);
        }

        generatePairs(emitted);
        emittedCount = emitted.size();
        emittedBucketCount = emitted.bucket_count();
    }

    // Pair数と重複除去Setの状態をAllocator Counterと同じProfiler frameへ登録します。
    // unordered_setはbucket配列に加えて要素ごとのnode確保を行うため、FrameAllocatorへ移すと
    // AllocationCount自体は増える場合があります。重要なのは通常heap allocationをPhysics Arenaへ
    // 集約できたことと、将来FlatHashSetへ置換した際の比較Baselineを得られることです。
    CPUProfiler::Get().AddCounter(
        "Physics.BroadPhase.PairCount",
        static_cast<double>(outPairs.size()));
    CPUProfiler::Get().AddCounter(
        "Physics.BroadPhase.ReservedPairCount",
        static_cast<double>(previousPairCount));
    CPUProfiler::Get().AddCounter(
        "Physics.BroadPhase.EmittedCount",
        static_cast<double>(emittedCount));
    CPUProfiler::Get().AddCounter(
        "Physics.BroadPhase.EmittedBucketCount",
        static_cast<double>(emittedBucketCount));

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