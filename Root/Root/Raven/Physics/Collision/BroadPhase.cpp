#include <algorithm>
#include <vector>

#include "Raven/Physics/Collision/BroadPhase.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
struct BroadPhaseEntry
{
    Entity EntityValue{};
    AABB Bounds{};
};
}

void BroadPhase::ComputePairs(Scene& scene, std::vector<BroadPhasePair>& outPairs) const
{
    outPairs.clear();
    std::vector<BroadPhaseEntry> entries;

    // Sphere / Boxだけが有限AABBを持ちます。無限Planeはここでは除外します。
    for (auto [entity, transform, collider]
        : scene.View<TransformComponent, ColliderComponent>())
    {
        AABB bounds{};
        if (ComputeColliderAABB(transform, collider, bounds))
        {
            entries.push_back(BroadPhaseEntry{ entity, bounds });
        }
    }

    // Sweep-and-Pruneの主軸としてXを使用します。
    std::sort(entries.begin(), entries.end(),
        [](const BroadPhaseEntry& lhs, const BroadPhaseEntry& rhs)
        {
            if (lhs.Bounds.Min.x != rhs.Bounds.Min.x)
            {
                return lhs.Bounds.Min.x < rhs.Bounds.Min.x;
            }
            return lhs.EntityValue.GetIndex() < rhs.EntityValue.GetIndex();
        });

    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const BroadPhaseEntry& a = entries[i];

        for (std::size_t j = i + 1; j < entries.size(); ++j)
        {
            const BroadPhaseEntry& b = entries[j];

            // Min.x順なので、この条件を満たした後の要素もすべてAから離れています。
            // ここでbreakすることで、不要な全組み合わせ検査を削減します。
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

} // namespace Raven::ph
