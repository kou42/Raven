#pragma once

#include <cstdint>
#include <limits>

#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Scene/Entity.h"

namespace Raven::ph
{

// DynamicAABBTreeで「参照先なし」を表すIndexです。
// raw pointerではなくIndexを使うことで、将来Node Poolをvector/固定配列の
// どちらで管理してもTreeの接続情報を安定して保持できます。
inline constexpr uint32_t InvalidTreeNode = std::numeric_limits<uint32_t>::max();

// ============================================================================
// DynamicAABBTreeNode
// ============================================================================
// Leafの場合:
//   - Bounds     : ColliderのFat AABB
//   - EntityValue: 対応するScene Entity
//   - Child1/2   : Invalid
//
// Branchの場合:
//   - Bounds     : Child1とChild2をCombineしたAABB
//   - EntityValue: 使用しない
//   - Child1/2   : 子Node Index
//
// HeightはTree balance判定用です。Leaf=0、free node=-1という規約にすると、
// 後でNode Poolを導入した際にも再利用判定を簡単にできます。
struct DynamicAABBTreeNode
{
    AABB Bounds{};

    uint32_t Parent = InvalidTreeNode;
    uint32_t Child1 = InvalidTreeNode;
    uint32_t Child2 = InvalidTreeNode;

    int32_t Height = 0;
    Entity EntityValue{};

    bool IsLeaf() const
    {
        return Child1 == InvalidTreeNode && Child2 == InvalidTreeNode;
    }
};

} // namespace Raven::ph
