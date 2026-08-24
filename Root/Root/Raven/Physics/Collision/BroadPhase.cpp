#include "Raven/Physics/Collision/BroadPhase.h"

#include "Raven/Core/Containers/FlatHashSet.h"
#include "Raven/Core/CPUProfiler.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
// std::unordered_setとFlatHashSetではfind()の戻り値が異なるため、
// Synchronize本体から実装差を隠す小さなAdapterを用意します。
bool ContainsSeen(const FlatHashSet<uint64_t>& seen, uint64_t entityValue)
{
    return seen.contains(entityValue);
}

template<typename SetType>
bool ContainsSeen(const SetType& seen, uint64_t entityValue)
{
    return seen.find(entityValue) != seen.end();
}
} // namespace

CollisionIgnorePairKey BroadPhase::MakeIgnorePairKey(Entity a, Entity b)
{
    uint64_t valueA = a.GetValue();
    uint64_t valueB = b.GetValue();

    if (valueA > valueB)
    {
        std::swap(valueA, valueB);
    }

    return CollisionIgnorePairKey{ valueA, valueB };
}

void BroadPhase::AddIgnorePair(Entity a, Entity b)
{
    // 同一Entity同士や無効Handleは衝突候補として意味を持たないため登録しません。
    // EntityHandle::Value()にはGenerationも含まれるため、Index再利用にも安全です。
    if (static_cast<bool>(a) == false
        || static_cast<bool>(b) == false
        || a.GetValue() == b.GetValue())
    {
        return;
    }

    m_IgnorePairs.insert(MakeIgnorePairKey(a, b));
}

void BroadPhase::RemoveIgnorePair(Entity a, Entity b)
{
    if (static_cast<bool>(a) == false || static_cast<bool>(b) == false)
    {
        return;
    }

    m_IgnorePairs.erase(MakeIgnorePairKey(a, b));
}

void BroadPhase::RemoveIgnorePairsForEntity(Entity entity)
{
    if (static_cast<bool>(entity) == false)
    {
        return;
    }

    const uint64_t entityValue = entity.GetValue();
    for (auto iterator = m_IgnorePairs.begin(); iterator != m_IgnorePairs.end(); )
    {
        if (iterator->A == entityValue || iterator->B == entityValue)
        {
            iterator = m_IgnorePairs.erase(iterator);
            continue;
        }

        ++iterator;
    }
}

bool BroadPhase::IsPairIgnored(Entity a, Entity b) const
{
    if (static_cast<bool>(a) == false || static_cast<bool>(b) == false)
    {
        return false;
    }

    return m_IgnorePairs.find(MakeIgnorePairKey(a, b)) != m_IgnorePairs.end();
}

void BroadPhase::ClearIgnorePairs()
{
    m_IgnorePairs.clear();
}

void BroadPhase::Synchronize(Scene& scene, Allocator* temporaryAllocator)
{
    // ========================================================================
    // Synchronize Call Counter
    // ========================================================================
    // ComputePairs / QueryAABB / RayCastはそれぞれ同期を要求できます。
    // 同じApplication frame内で何度呼ばれているかを観測し、Allocator最適化後に
    // 「同期そのものの重複実行」を削減する価値があるか判断できるようにします。
    CPUProfiler::Get().AddCounter("Physics.BroadPhase.SynchronizeCallCount", 1.0);

    // 実際のScene同期処理はSet実装に依存しないようlambdaへまとめます。
    // FrameAllocator経路ではFlatHashSet、Allocatorを持たないQuery系では
    // 従来どおりstd::unordered_setを使用します。
    const auto synchronizeWithSeen =
        [&](auto& seen)
        {
            for (auto [entity, transform, collider]
                : scene.View<TransformComponent, ColliderComponent>())
            {
                AABB tightBounds{};
                if (ComputeColliderAABB(transform, collider, tightBounds) == false)
                {
                    continue;
                }

                const uint64_t entityValue = entity.GetValue();
                seen.insert(entityValue);

                const auto found = m_Proxies.find(entityValue);
                if (found == m_Proxies.end())
                {
                    // 新規Entityはプロキシ作成。次フレームの移動量計算用に中心も保存します。
                    const uint32_t proxy = m_Tree.CreateProxy(tightBounds, entity);
                    m_Proxies.emplace(entityValue, proxy);
                    m_PreviousCenters[entityValue] = tightBounds.GetCenter();
                    continue;
                }

                const math::Vec3 center = tightBounds.GetCenter();
                const math::Vec3 previousCenter = m_PreviousCenters[entityValue];
                const math::Vec3 displacement = center - previousCenter;

                // 既存Entityはtight AABBと移動量で更新。Fat AABB内ならTree再挿入は省略されます。
                m_Tree.MoveProxy(found->second, tightBounds, displacement);
                m_PreviousCenters[entityValue] = center;
            }

            // Sceneから消えたEntityのプロキシを破棄し、孤立ノードを残さないようにします。
            for (auto iterator = m_Proxies.begin(); iterator != m_Proxies.end(); )
            {
                if (ContainsSeen(seen, iterator->first))
                {
                    ++iterator;
                    continue;
                }

                m_Tree.DestroyProxy(iterator->second);
                m_PreviousCenters.erase(iterator->first);
                iterator = m_Proxies.erase(iterator);
            }
        };

    std::size_t seenCount = 0;
    std::size_t seenStorageCapacity = 0;

    if (temporaryAllocator != nullptr)
    {
        // ====================================================================
        // Physics FrameAllocator Path
        // ====================================================================
        // m_Proxies.size()は直前同期時のCollider数なので、次回のseen要素数の予測値として
        // 利用します。大きなScene変化がなければSlot配列を1 allocationで確保できます。
        FlatHashSet<uint64_t> seen(*temporaryAllocator);
        const std::size_t previousProxyCount = m_Proxies.size();
        if (previousProxyCount > 0)
        {
            seen.reserve(previousProxyCount);
        }

        synchronizeWithSeen(seen);
        seenCount = seen.size();
        seenStorageCapacity = seen.capacity();
    }
    else
    {
        // QueryAABB / RayCastなど、Physics FrameAllocatorを明示的に受け取らない経路は
        // 互換性を優先して従来の標準Setを使用します。
        std::unordered_set<uint64_t> seen;
        const std::size_t previousProxyCount = m_Proxies.size();
        if (previousProxyCount > 0)
        {
            seen.reserve(previousProxyCount);
        }

        synchronizeWithSeen(seen);
        seenCount = seen.size();
        seenStorageCapacity = seen.bucket_count();
    }

    CPUProfiler::Get().AddCounter(
        "Physics.BroadPhase.SynchronizeSeenCount",
        static_cast<double>(seenCount));
    CPUProfiler::Get().AddCounter(
        "Physics.BroadPhase.SynchronizeSeenStorageCapacity",
        static_cast<double>(seenStorageCapacity));

    // Entity破棄後にIgnore Pairだけが残り続けないよう、Generation込みHandleで生存確認します。
    for (auto iterator = m_IgnorePairs.begin(); iterator != m_IgnorePairs.end(); )
    {
        const bool aliveA = scene.IsEntityAlive(EntityHandle::FromValue(iterator->A));
        const bool aliveB = scene.IsEntityAlive(EntityHandle::FromValue(iterator->B));
        if (aliveA == false || aliveB == false)
        {
            iterator = m_IgnorePairs.erase(iterator);
            continue;
        }

        ++iterator;
    }
}

} // namespace Raven::ph