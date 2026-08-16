#include "Raven/Physics/Collision/BroadPhase.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{

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

// Broad Phase主処理:
// Sceneとの同期後にFat AABB同士の候補ペアを収集し、
// Narrow Phaseへ渡す重複なしのペア列を生成します。
void BroadPhase::ComputePairs(Scene& scene, std::vector<BroadPhasePair>& outPairs)
{
    Synchronize(scene);
    outPairs.clear();

    // Pairを順序正規化して保持し、(A,B) / (B,A) の重複通知を防ぎます。
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
    // これにより画面に出るPairと、同じPhysics StepでNarrow Phaseへ渡されたPairが
    // 完全に一致します。
    m_LastPairs = outPairs;
}

void BroadPhase::Synchronize(Scene& scene)
{
    // Sceneの実体とTreeプロキシを同期し、生成・移動・削除を反映します。
    std::unordered_set<uint64_t> seen;

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
        if (seen.find(iterator->first) != seen.end())
        {
            ++iterator;
            continue;
        }

        m_Tree.DestroyProxy(iterator->second);
        m_PreviousCenters.erase(iterator->first);
        iterator = m_Proxies.erase(iterator);
    }

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