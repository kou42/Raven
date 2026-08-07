#pragma once

#include <cstdint>

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
DynamicAABBTreeValidationResult ValidateDynamicAABBTree(
    const DynamicAABBTree& tree);

} // namespace Raven::ph
