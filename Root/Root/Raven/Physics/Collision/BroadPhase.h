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
// CollisionIgnorePairKey
// ============================================================================
// EntityHandle::Value()を小さい順に並べた、順序非依存の衝突除外キーです。
// GenerationもValue()へ含まれるため、Entity Indexが再利用されても古い除外設定が
// 新しいEntityへ誤って引き継がれません。
struct CollisionIgnorePairKey
{
    uint64_t A = 0;
    uint64_t B = 0;

    bool operator==(const CollisionIgnorePairKey& rhs) const
    {
        return A == rhs.A && B == rhs.B;
    }
};

struct CollisionIgnorePairKeyHasher
{
    std::size_t operator()(const CollisionIgnorePairKey& key) const noexcept
    {
        std::size_t seed = std::hash<uint64_t>{}(key.A);
        seed ^= std::hash<uint64_t>{}(key.B)
            + static_cast<std::size_t>(0x9e3779b97f4a7c15ull)
            + (seed << 6) + (seed >> 2);
        return seed;
    }
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
    // Broad Phase主処理:
    // Sceneとの同期後にFat AABB同士の候補ペアを収集し、Narrow Phaseへ渡す
    // 重複なしのPair列を生成します。
    //
    // 出力Containerは clear() / push_back() / begin() / end() を満たせばよく、
    // std::vectorのAllocator型には依存しません。これにより通常vectorを使う既存経路を
    // 保ったまま、Physicsのフレーム一時PairだけをFrameAllocatorへ移行できます。
    template <class PairContainer>
    void ComputePairs(Scene& scene, PairContainer& outPairs)
    {
        Synchronize(scene);
        outPairs.clear();

        // Pairを順序正規化して保持し、(A,B) / (B,A) の重複通知を防ぎます。
        // emitted自体は現段階では通常heapを使用し、Pair列の移行効果を先に分離計測します。
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

    // ========================================================================
    // Collision Ignore Pair
    // ========================================================================
    // Jointで接続されたRagdoll Bodyなど、「形状は重なっても衝突応答させたくない」
    // Entityペアを登録します。A/Bの順序は問いません。
    void AddIgnorePair(Entity a, Entity b);
    void RemoveIgnorePair(Entity a, Entity b);
    void RemoveIgnorePairsForEntity(Entity entity);
    bool IsPairIgnored(Entity a, Entity b) const;
    void ClearIgnorePairs();

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
    static CollisionIgnorePairKey MakeIgnorePairKey(Entity a, Entity b);
    void Synchronize(Scene& scene);

private:
    DynamicAABBTree m_Tree;
    std::unordered_map<uint64_t, uint32_t> m_Proxies;
    std::unordered_map<uint64_t, math::Vec3> m_PreviousCenters;

    // Ignore PairはBroad Phase候補をNarrow Phaseへ渡す直前に適用します。
    // Tree自体からProxyを消す方式ではないため、RayCast / QueryAABBには影響しません。
    std::unordered_set<CollisionIgnorePairKey, CollisionIgnorePairKeyHasher> m_IgnorePairs;

    // Simulationで最後に生成されたPairの診断用Snapshotです。
    // FrameAllocatorのReset後もDebug Overlayから参照するため、ここは永続vectorです。
    std::vector<BroadPhasePair> m_LastPairs;
};

} // namespace Raven::ph
