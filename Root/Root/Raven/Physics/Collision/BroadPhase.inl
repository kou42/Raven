#pragma once

#include "Raven/Scene/Scene.h"

namespace Raven::ph
{

template <class Callback>
void BroadPhase::QueryAABB(Scene& scene, const AABB& queryBounds, Callback&& callback)
{
    // Query前にScene同期を必ず実行し、古いProxy状態での取りこぼしを防ぎます。
    Synchronize(scene);
    m_Tree.Query(queryBounds,
        [&](Entity entity, uint32_t proxyId) -> bool
        {
            if (!scene.IsEntityAlive(entity))
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
            if (!scene.IsEntityAlive(entity))
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