#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Physics/Collision/DynamicAABBTree.h"

namespace Raven::ph
{

// ============================================================================
// DynamicAABBTreeValidationResult
// ============================================================================
// Dynamic AABB Treeの内部不整合をDebug時に検出するための軽量レポートです。
// ErrorCountが0ならIsValid()はtrueです。
struct DynamicAABBTreeValidationResult
{
    uint32_t ReachableNodeCount = 0;
    uint32_t LeafCount = 0;
    uint32_t BranchCount = 0;
    uint32_t FreeNodeCount = 0;
    uint32_t ErrorCount = 0;
    int32_t ComputedHeight = 0;

    bool IsValid() const
    {
        return ErrorCount == 0;
    }
};

namespace detail
{

inline bool EqualAABB(const AABB& a, const AABB& b)
{
    // Combine()はmin/maxだけで決定されるため、ここではepsilon比較ではなく
    // 完全一致で構造的なBounds更新漏れを検出します。
    return a.Min == b.Min && a.Max == b.Max;
}

inline int32_t ValidateDynamicAABBTreeNode(
    const DynamicAABBTree& tree,
    uint32_t nodeId,
    uint32_t expectedParent,
    std::vector<uint8_t>& visitState,
    DynamicAABBTreeValidationResult& result)
{
    const auto& nodes = tree.GetNodes();

    if (nodeId == InvalidTreeNode || nodeId >= nodes.size())
    {
        ++result.ErrorCount;
        return 0;
    }

    // 0 = 未訪問, 1 = 探索中, 2 = 探索完了
    // 探索中Nodeへ再到達した場合はParent/Childが循環しています。
    if (visitState[nodeId] == 1)
    {
        ++result.ErrorCount;
        return 0;
    }

    // 同じNodeが異なる親から共有されている場合もTreeでは不正です。
    if (visitState[nodeId] == 2)
    {
        ++result.ErrorCount;
        return nodes[nodeId].Height;
    }

    const DynamicAABBTreeNode& node = nodes[nodeId];

    // Height < 0 はFree Nodeなので、Tree traversalから到達してはいけません。
    if (node.Height < 0)
    {
        ++result.ErrorCount;
        return 0;
    }

    visitState[nodeId] = 1;
    ++result.ReachableNodeCount;

    if (node.Parent != expectedParent)
    {
        ++result.ErrorCount;
    }

    if (!node.Bounds.IsValid())
    {
        ++result.ErrorCount;
    }

    if (node.IsLeaf())
    {
        ++result.LeafCount;

        // Leafの高さは必ず0です。
        if (node.Height != 0)
        {
            ++result.ErrorCount;
        }

        visitState[nodeId] = 2;
        return 0;
    }

    ++result.BranchCount;

    // Branchは必ず2つの異なるChildを持ちます。
    if (node.Child1 == InvalidTreeNode
        || node.Child2 == InvalidTreeNode
        || node.Child1 == node.Child2)
    {
        ++result.ErrorCount;
        visitState[nodeId] = 2;
        return node.Height;
    }

    const int32_t height1 = ValidateDynamicAABBTreeNode(
        tree, node.Child1, nodeId, visitState, result);
    const int32_t height2 = ValidateDynamicAABBTreeNode(
        tree, node.Child2, nodeId, visitState, result);

    const int32_t expectedHeight = 1 + (height1 > height2 ? height1 : height2);
    if (node.Height != expectedHeight)
    {
        ++result.ErrorCount;
    }

    if (node.Child1 < nodes.size() && node.Child2 < nodes.size())
    {
        const AABB expectedBounds = AABB::Combine(
            nodes[node.Child1].Bounds,
            nodes[node.Child2].Bounds);

        // Branch Boundsは「2つの子をぴったり包むAABB」である必要があります。
        // ここがずれるとQuery/RayCastの枝刈り結果が壊れます。
        if (!EqualAABB(node.Bounds, expectedBounds))
        {
            ++result.ErrorCount;
        }
    }

    visitState[nodeId] = 2;
    return expectedHeight;
}

} // namespace detail

// ============================================================================
// ValidateDynamicAABBTree
// ============================================================================
// 以下をまとめて検証します。
//   - Root Parent
//   - Parent / Child双方向整合性
//   - 循環参照 / 複数Parent共有
//   - Leaf / BranchのHeight
//   - Branch AABB = Combine(Child1, Child2)
//   - Treeから到達できないAllocated Node
//   - Free NodeがTreeへ紛れ込んでいないこと
//
// Dynamic TreeのBalance実装は局所回転を多用するため、見た目には正常でも
// Parent更新漏れがあると数フレーム後に破綻しやすいです。そのためNarrow Phaseを
// 増やす前に、この検証をDebug Drawと一緒に常用できるようにしています。
inline DynamicAABBTreeValidationResult ValidateDynamicAABBTree(
    const DynamicAABBTree& tree)
{
    DynamicAABBTreeValidationResult result{};
    const auto& nodes = tree.GetNodes();

    if (tree.GetRoot() == InvalidTreeNode)
    {
        for (const DynamicAABBTreeNode& node : nodes)
        {
            if (node.Height >= 0)
            {
                ++result.ErrorCount;
            }
            else
            {
                ++result.FreeNodeCount;
            }
        }
        return result;
    }

    if (tree.GetRoot() >= nodes.size())
    {
        ++result.ErrorCount;
        return result;
    }

    if (nodes[tree.GetRoot()].Parent != InvalidTreeNode)
    {
        ++result.ErrorCount;
    }

    std::vector<uint8_t> visitState(nodes.size(), 0);
    result.ComputedHeight = detail::ValidateDynamicAABBTreeNode(
        tree,
        tree.GetRoot(),
        InvalidTreeNode,
        visitState,
        result);

    // Pool内の全Nodeも確認します。
    // AllocatedなのにRootから到達できないNodeはRemove/Balance処理の接続漏れです。
    for (uint32_t i = 0; i < static_cast<uint32_t>(nodes.size()); ++i)
    {
        if (nodes[i].Height < 0)
        {
            ++result.FreeNodeCount;
            if (visitState[i] != 0)
            {
                ++result.ErrorCount;
            }
            continue;
        }

        if (visitState[i] == 0)
        {
            ++result.ErrorCount;
        }
    }

    return result;
}

} // namespace Raven::ph
