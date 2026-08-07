#pragma once

#include "Raven/Scene/Scene.h"

namespace Raven::ph
{

template <class Callback>
void BroadPhase::QueryAABB(Scene& scene, const AABB& queryBounds, Callback&& callback)
{
    Synchronize(scene);
    m_Tree.Query(queryBounds,
        [&](Entity entity, uint32_t proxyId) -> bool
        {
            if (!scene.IsEntityAlive(entity))
            {
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
                return currentMaxFraction;
            }

            return callback(
                entity,
                proxyId,
                fraction,
                normal,
                currentMaxFraction);
        });
}

} // namespace Raven::ph