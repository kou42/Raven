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

// ============================================================================
// BroadPhase
// ============================================================================
// Dynamic AABB Treeを保持し、Scene内Colliderの追加・移動・削除をProxyへ同期します。
//
// 以前のSweep-and-PruneはComputePairs()ごとに全Colliderを収集・sortしていました。
// Dynamic TreeではFat AABB内の微小移動ならTree構造を変更せず、必要なLeafだけを
// 再挿入します。
class BroadPhase
{
public:
    void ComputePairs(Scene& scene, std::vector<BroadPhasePair>& outPairs);

    // 直近のComputePairs()がSimulationへ返した候補Pairを保持します。
    // Debug描画のためにComputePairs()を再実行するとTreeの同期タイミングが変わるため、
    // OverlayはこのSnapshotを読み取ります。
    const std::vector<BroadPhasePair>& GetLastPairs() const { return m_LastPairs; }

    // ------------------------------------------------------------------------
    // QueryAABB
    // ------------------------------------------------------------------------
    // Dynamic TreeからqueryBoundsと重なる「候補Leaf」を列挙します。
    // Fat AABBを検索しているため、この段階ではfalse positiveを許容します。
    // PhysicsWorld側でtight AABB / 実Colliderを使って最終判定してください。
    template <class Callback>
    void QueryAABB(Scene& scene, const AABB& queryBounds, Callback&& callback);

    // ------------------------------------------------------------------------
    // RayCast
    // ------------------------------------------------------------------------
    // Dynamic TreeのFat AABBに対してRay traversalを行い、候補Leafだけを通知します。
    // callbackが返すfractionを縮めることで、より遠いBranchを早期枝刈りできます。
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
    void Synchronize(Scene& scene);

private:
    DynamicAABBTree m_Tree;
    std::unordered_map<uint64_t, uint32_t> m_Proxies;
    std::unordered_map<uint64_t, math::Vec3> m_PreviousCenters;

    // Simulationで最後に生成されたPairの診断用Snapshotです。
    std::vector<BroadPhasePair> m_LastPairs;
};

} // namespace Raven::ph
