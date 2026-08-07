#pragma once

#include <vector>

#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{
class Scene;
}

namespace Raven::ph
{

// Narrow Phaseへ渡す「衝突する可能性があるEntityの組」です。
// Broad Phaseでは実際に形状が衝突しているかまでは確定しません。
struct BroadPhasePair
{
    Entity A{};
    Entity B{};
};

// ============================================================================
// BroadPhase
// ============================================================================
// 有限Collider(Sphere / Box)のAABBを作り、Sweep-and-Pruneで候補ペアを生成します。
//
// 処理の流れ
//   1. SceneからColliderを収集してAABBを生成
//   2. AABB::Min.xでソート
//   3. X軸で重なる可能性がある間だけ比較
//   4. 最後にY/Zも含むAABB重なり判定
//
// これにより、Narrow Phaseが全Collider組を総当たりする必要をなくします。
class BroadPhase
{
public:
    void ComputePairs(Scene& scene, std::vector<BroadPhasePair>& outPairs) const;
};

} // namespace Raven::ph
