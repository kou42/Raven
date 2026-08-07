#pragma once

#include <algorithm>
#include <vector>

#include "Raven/Physics/Collision/AABB.h"
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
// Sphere / BoxのAABBをX軸Sweep-and-Pruneで絞り込み、Narrow Phase候補を作ります。
class BroadPhase
{
public:
    void ComputePairs(Scene& scene, std::vector<BroadPhasePair>& outPairs) const
    {
        struct Entry
        {
            Entity EntityValue{};
            AABB Bounds{};
        };

        outPairs.clear();
        std::vector<Entry> entries;

        for (auto [entity, transform, collider]
            : scene.View<TransformComponent, ColliderComponent>())
        {
            AABB bounds{};
            if (ComputeColliderAABB(transform, collider, bounds))
            {
                entries.push_back(Entry{ entity, bounds });
            }
        }

        // X軸の開始位置で並べます。同値時はEntity Indexで順序を固定します。
        std::sort(entries.begin(), entries.end(),
            [](const Entry& lhs, const Entry& rhs)
            {
                if (lhs.Bounds.Min.x != rhs.Bounds.Min.x)
                {
                    return lhs.Bounds.Min.x < rhs.Bounds.Min.x;
                }
                return lhs.EntityValue.GetIndex() < rhs.EntityValue.GetIndex();
            });

        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const Entry& a = entries[i];

            for (std::size_t j = i + 1; j < entries.size(); ++j)
            {
                const Entry& b = entries[j];

                // Min.x順なので、Bの左端がAの右端を越えた後の要素は
                // すべてAとX軸で交差しません。ここで打ち切れるのがSAPの要点です。
                if (b.Bounds.Min.x > a.Bounds.Max.x)
                {
                    break;
                }

                if (!a.Bounds.Overlaps(b.Bounds))
                {
                    continue;
                }

                BroadPhasePair pair{ a.EntityValue, b.EntityValue };
                if (pair.A.GetIndex() > pair.B.GetIndex())
                {
                    std::swap(pair.A, pair.B);
                }
                outPairs.push_back(pair);
            }
        }
    }
};

} // namespace Raven::ph
