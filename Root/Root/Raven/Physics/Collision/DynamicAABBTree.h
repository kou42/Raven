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
        const math::Vec3& displacement = math::Vec3{})
    {
        const uint32_t proxyId = AllocateNode();
        DynamicAABBTreeNode& node = m_Nodes[proxyId];
        node.Bounds = AABB::CreateFat(
            bounds,
            m_FatMargin,
            displacement,
            m_DisplacementMultiplier);
        node.EntityValue = entity;
        node.Height = 0;

        InsertLeaf(proxyId);
        return proxyId;
    }

    void DestroyProxy(uint32_t proxyId)
    {
        if (!IsAllocated(proxyId))
        {
            return;
        }

        RemoveLeaf(proxyId);
        FreeNode(proxyId);
    }

    // tightBoundsが既存Fat AABB内ならTree構造を変更しません。
    // false: 再挿入不要 / true: Remove + Insertを実行した、という戻り値です。
    bool MoveProxy(
        uint32_t proxyId,
        const AABB& tightBounds,
        const math::Vec3& displacement)
    {
        if (!IsAllocated(proxyId))
        {
            return false;
        }

        DynamicAABBTreeNode& node = m_Nodes[proxyId];
        if (node.Bounds.Contains(tightBounds))
        {
            return false;
        }

        RemoveLeaf(proxyId);
        node.Bounds = AABB::CreateFat(
            tightBounds,
            m_FatMargin,
            displacement,
            m_DisplacementMultiplier);
        InsertLeaf(proxyId);
        return true;
    }

    const AABB& GetFatAABB(uint32_t proxyId) const
    {
        return m_Nodes[proxyId].Bounds;
    }

    Entity GetEntity(uint32_t proxyId) const
    {
        return m_Nodes[proxyId].EntityValue;
    }

    uint32_t GetRoot() const { return m_Root; }
    const std::vector<DynamicAABBTreeNode>& GetNodes() const { return m_Nodes; }

    void SetFatMargin(float margin) { m_FatMargin = std::max(margin, 0.0f); }
    float GetFatMargin() const { return m_FatMargin; }

    void SetDisplacementMultiplier(float multiplier)
    {
        m_DisplacementMultiplier = std::max(multiplier, 0.0f);
    }

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
    bool IsAllocated(uint32_t nodeId) const
    {
        return nodeId < m_Nodes.size() && m_Nodes[nodeId].Height >= 0;
    }

    uint32_t AllocateNode()
    {
        uint32_t nodeId = InvalidTreeNode;

        if (!m_FreeList.empty())
        {
            nodeId = m_FreeList.back();
            m_FreeList.pop_back();
            m_Nodes[nodeId] = DynamicAABBTreeNode{};
        }
        else
        {
            nodeId = static_cast<uint32_t>(m_Nodes.size());
            m_Nodes.emplace_back();
        }

        m_Nodes[nodeId].Height = 0;
        return nodeId;
    }

    void FreeNode(uint32_t nodeId)
    {
        DynamicAABBTreeNode& node = m_Nodes[nodeId];
        node = DynamicAABBTreeNode{};
        node.Height = -1;
        m_FreeList.push_back(nodeId);
    }

    void InsertLeaf(uint32_t leaf)
    {
        if (m_Root == InvalidTreeNode)
        {
            m_Root = leaf;
            m_Nodes[leaf].Parent = InvalidTreeNode;
            return;
        }

        // ====================================================================
        // SAHで最も挿入コストが小さいSiblingを探す
        // ====================================================================
        const AABB leafBounds = m_Nodes[leaf].Bounds;
        uint32_t sibling = m_Root;

        while (!m_Nodes[sibling].IsLeaf())
        {
            const DynamicAABBTreeNode& node = m_Nodes[sibling];
            const uint32_t child1 = node.Child1;
            const uint32_t child2 = node.Child2;

            const float oldArea = node.Bounds.SurfaceArea();
            const AABB combined = AABB::Combine(node.Bounds, leafBounds);
            const float combinedArea = combined.SurfaceArea();

            // このNode直下へ新Parentを置く場合のコスト。
            const float parentCost = 2.0f * combinedArea;

            // 既存Branchを1段下へ押し込むことで増える継承コスト。
            const float inheritanceCost = 2.0f * (combinedArea - oldArea);

            const float cost1 = DescendCost(child1, leafBounds, inheritanceCost);
            const float cost2 = DescendCost(child2, leafBounds, inheritanceCost);

            if (parentCost < cost1 && parentCost < cost2)
            {
                break;
            }

            sibling = (cost1 < cost2) ? child1 : child2;
        }

        const uint32_t oldParent = m_Nodes[sibling].Parent;
        const uint32_t newParent = AllocateNode();

        DynamicAABBTreeNode& parent = m_Nodes[newParent];
        parent.Parent = oldParent;
        parent.Bounds = AABB::Combine(leafBounds, m_Nodes[sibling].Bounds);
        parent.Height = m_Nodes[sibling].Height + 1;
        parent.Child1 = sibling;
        parent.Child2 = leaf;

        m_Nodes[sibling].Parent = newParent;
        m_Nodes[leaf].Parent = newParent;

        if (oldParent == InvalidTreeNode)
        {
            m_Root = newParent;
        }
        else
        {
            DynamicAABBTreeNode& grandParent = m_Nodes[oldParent];
            if (grandParent.Child1 == sibling)
            {
                grandParent.Child1 = newParent;
            }
            else
            {
                grandParent.Child2 = newParent;
            }
        }

        FixUpwards(newParent);
    }

    float DescendCost(uint32_t child, const AABB& leafBounds, float inheritanceCost) const
    {
        const DynamicAABBTreeNode& childNode = m_Nodes[child];
        const AABB combined = AABB::Combine(childNode.Bounds, leafBounds);

        if (childNode.IsLeaf())
        {
            return combined.SurfaceArea() + inheritanceCost;
        }

        return (combined.SurfaceArea() - childNode.Bounds.SurfaceArea()) + inheritanceCost;
    }

    void RemoveLeaf(uint32_t leaf)
    {
        if (leaf == m_Root)
        {
            m_Root = InvalidTreeNode;
            m_Nodes[leaf].Parent = InvalidTreeNode;
            return;
        }

        const uint32_t parent = m_Nodes[leaf].Parent;
        const uint32_t grandParent = m_Nodes[parent].Parent;
        const uint32_t sibling =
            (m_Nodes[parent].Child1 == leaf)
            ? m_Nodes[parent].Child2
            : m_Nodes[parent].Child1;

        if (grandParent != InvalidTreeNode)
        {
            DynamicAABBTreeNode& grand = m_Nodes[grandParent];
            if (grand.Child1 == parent)
            {
                grand.Child1 = sibling;
            }
            else
            {
                grand.Child2 = sibling;
            }

            m_Nodes[sibling].Parent = grandParent;
            FreeNode(parent);
            FixUpwards(grandParent);
        }
        else
        {
            m_Root = sibling;
            m_Nodes[sibling].Parent = InvalidTreeNode;
            FreeNode(parent);
        }

        m_Nodes[leaf].Parent = InvalidTreeNode;
    }

    void FixUpwards(uint32_t nodeId)
    {
        while (nodeId != InvalidTreeNode)
        {
            nodeId = Balance(nodeId);

            DynamicAABBTreeNode& node = m_Nodes[nodeId];
            if (!node.IsLeaf())
            {
                const DynamicAABBTreeNode& child1 = m_Nodes[node.Child1];
                const DynamicAABBTreeNode& child2 = m_Nodes[node.Child2];
                node.Height = 1 + std::max(child1.Height, child2.Height);
                node.Bounds = AABB::Combine(child1.Bounds, child2.Bounds);
            }

            nodeId = node.Parent;
        }
    }

    // AVL Treeと同じ発想で、左右の高さ差が2以上なら局所回転します。
    // AABBも回転後の子構成に合わせて必ず再計算します。
    uint32_t Balance(uint32_t aId)
    {
        DynamicAABBTreeNode& a = m_Nodes[aId];
        if (a.IsLeaf() || a.Height < 2)
        {
            return aId;
        }

        const uint32_t bId = a.Child1;
        const uint32_t cId = a.Child2;
        DynamicAABBTreeNode& b = m_Nodes[bId];
        DynamicAABBTreeNode& c = m_Nodes[cId];

        const int balance = c.Height - b.Height;

        // Cを上へ回転。
        if (balance > 1)
        {
            const uint32_t fId = c.Child1;
            const uint32_t gId = c.Child2;
            DynamicAABBTreeNode& f = m_Nodes[fId];
            DynamicAABBTreeNode& g = m_Nodes[gId];

            c.Child1 = aId;
            c.Parent = a.Parent;
            a.Parent = cId;

            if (c.Parent != InvalidTreeNode)
            {
                DynamicAABBTreeNode& parent = m_Nodes[c.Parent];
                if (parent.Child1 == aId) parent.Child1 = cId;
                else parent.Child2 = cId;
            }
            else
            {
                m_Root = cId;
            }

            if (f.Height > g.Height)
            {
                c.Child2 = fId;
                a.Child2 = gId;
                g.Parent = aId;

                a.Bounds = AABB::Combine(b.Bounds, g.Bounds);
                c.Bounds = AABB::Combine(a.Bounds, f.Bounds);
                a.Height = 1 + std::max(b.Height, g.Height);
                c.Height = 1 + std::max(a.Height, f.Height);
            }
            else
            {
                c.Child2 = gId;
                a.Child2 = fId;
                f.Parent = aId;

                a.Bounds = AABB::Combine(b.Bounds, f.Bounds);
                c.Bounds = AABB::Combine(a.Bounds, g.Bounds);
                a.Height = 1 + std::max(b.Height, f.Height);
                c.Height = 1 + std::max(a.Height, g.Height);
            }

            return cId;
        }

        // Bを上へ回転。
        if (balance < -1)
        {
            const uint32_t dId = b.Child1;
            const uint32_t eId = b.Child2;
            DynamicAABBTreeNode& d = m_Nodes[dId];
            DynamicAABBTreeNode& e = m_Nodes[eId];

            b.Child1 = aId;
            b.Parent = a.Parent;
            a.Parent = bId;

            if (b.Parent != InvalidTreeNode)
            {
                DynamicAABBTreeNode& parent = m_Nodes[b.Parent];
                if (parent.Child1 == aId) parent.Child1 = bId;
                else parent.Child2 = bId;
            }
            else
            {
                m_Root = bId;
            }

            if (d.Height > e.Height)
            {
                b.Child2 = dId;
                a.Child1 = eId;
                e.Parent = aId;

                a.Bounds = AABB::Combine(c.Bounds, e.Bounds);
                b.Bounds = AABB::Combine(a.Bounds, d.Bounds);
                a.Height = 1 + std::max(c.Height, e.Height);
                b.Height = 1 + std::max(a.Height, d.Height);
            }
            else
            {
                b.Child2 = eId;
                a.Child1 = dId;
                d.Parent = aId;

                a.Bounds = AABB::Combine(c.Bounds, d.Bounds);
                b.Bounds = AABB::Combine(a.Bounds, e.Bounds);
                a.Height = 1 + std::max(c.Height, d.Height);
                b.Height = 1 + std::max(a.Height, e.Height);
            }

            return bId;
        }

        return aId;
    }

private:
    std::vector<DynamicAABBTreeNode> m_Nodes;
    std::vector<uint32_t> m_FreeList;
    uint32_t m_Root = InvalidTreeNode;

    float m_FatMargin = DefaultFatMargin;
    float m_DisplacementMultiplier = DefaultDisplacementMultiplier;
};

} // namespace Raven::ph
