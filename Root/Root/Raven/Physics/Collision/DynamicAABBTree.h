#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Raven/Physics/Collision/DynamicAABBTreeNode.h"

namespace Raven::ph
{

// ============================================================================
// DynamicAABBTree
// ============================================================================
// Broad Phase用の動的BVHです。
// LeafにはColliderのFat AABBを保持し、Branchには2つの子を包むAABBを保持します。
//
// 重要な設計方針
// ---------------------------------------------------------------------------
// 1. Nodeはraw pointerではなくIndexで接続する
// 2. Leafの実AABBがFat AABB内に収まる間はTreeを更新しない
// 3. Leaf挿入先はSurface Area Heuristic(SAH)で選ぶ
// 4. 更新後は祖先へ向かってBounds/Heightを再計算し、必要なら回転してBalanceする
//
// これにより、毎フレーム全ColliderをソートするSweep-and-Pruneとは異なり、
// 「動いたColliderの周辺だけ」を更新できるBroad Phaseになります。
class DynamicAABBTree
{
public:
    static constexpr float DefaultFatMargin = 0.1f;
    static constexpr float DefaultDisplacementMultiplier = 2.0f;

    uint32_t CreateProxy(
        const AABB& bounds,
        Entity entity,
        const math::Vec3& displacement = math::Vec3{});

    void DestroyProxy(uint32_t proxyId);

    // tightBoundsが既存Fat AABB内ならTree構造を変更しません。
    // false: 再挿入不要 / true: Remove + Insertを実行した、という戻り値です。
    bool MoveProxy(
        uint32_t proxyId,
        const AABB& tightBounds,
        const math::Vec3& displacement);

    const AABB& GetFatAABB(uint32_t proxyId) const;

    Entity GetEntity(uint32_t proxyId) const;

    uint32_t GetRoot() const;
    const std::vector<DynamicAABBTreeNode>& GetNodes() const;

    void SetFatMargin(float margin);
    float GetFatMargin() const;

    void SetDisplacementMultiplier(float multiplier);

    // AABBと重なるLeafをTree traversalで列挙します。
    // callback(Entity, proxyId) がfalseを返した時点で探索を終了します。
    template <class Callback>
    void Query(const AABB& bounds, Callback&& callback) const
    {
        if (m_Root == InvalidTreeNode)
        {
            return;
        }

        std::vector<uint32_t> stack;
        stack.push_back(m_Root);

        while (!stack.empty())
        {
            const uint32_t nodeId = stack.back();
            stack.pop_back();

            const DynamicAABBTreeNode& node = m_Nodes[nodeId];
            if (!node.Bounds.Overlaps(bounds))
            {
                continue;
            }

            if (node.IsLeaf())
            {
                if (!callback(node.EntityValue, nodeId))
                {
                    return;
                }
            }
            else
            {
                stack.push_back(node.Child1);
                stack.push_back(node.Child2);
            }
        }
    }

    // Tree全体に対するRay Castです。
    // callback(Entity, proxyId, fraction, normal, currentMaxFraction) は、
    // 次の探索上限fractionを返します。
    //   0以下             : 探索終了
    //   currentMax以下    : それより遠いNodeを枝刈り
    //   currentMaxより大  : currentMaxを維持
    template <class Callback>
    void RayCast(
        const math::Vec3& origin,
        const math::Vec3& direction,
        float maxFraction,
        Callback&& callback) const
    {
        if (m_Root == InvalidTreeNode || maxFraction < 0.0f)
        {
            return;
        }

        float currentMax = maxFraction;
        std::vector<uint32_t> stack;
        stack.push_back(m_Root);

        while (!stack.empty())
        {
            const uint32_t nodeId = stack.back();
            stack.pop_back();

            const DynamicAABBTreeNode& node = m_Nodes[nodeId];
            float fraction = 0.0f;
            math::Vec3 normal{};
            if (!node.Bounds.RayCast(origin, direction, currentMax, fraction, &normal))
            {
                continue;
            }

            if (node.IsLeaf())
            {
                const float requestedMax = callback(
                    node.EntityValue,
                    nodeId,
                    fraction,
                    normal,
                    currentMax);

                if (requestedMax <= 0.0f)
                {
                    return;
                }

                currentMax = std::min(currentMax, requestedMax);
            }
            else
            {
                stack.push_back(node.Child1);
                stack.push_back(node.Child2);
            }
        }
    }

private:
    bool IsAllocated(uint32_t nodeId) const;

    uint32_t AllocateNode();

    void FreeNode(uint32_t nodeId);

    void InsertLeaf(uint32_t leaf);

    float DescendCost(uint32_t child, const AABB& leafBounds, float inheritanceCost) const;

    void RemoveLeaf(uint32_t leaf);

    void FixUpwards(uint32_t nodeId);

    uint32_t Balance(uint32_t aId);

private:
    std::vector<DynamicAABBTreeNode> m_Nodes;
    std::vector<uint32_t> m_FreeList;
    uint32_t m_Root = InvalidTreeNode;

    float m_FatMargin = DefaultFatMargin;
    float m_DisplacementMultiplier = DefaultDisplacementMultiplier;
};

} // namespace Raven::ph
