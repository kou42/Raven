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