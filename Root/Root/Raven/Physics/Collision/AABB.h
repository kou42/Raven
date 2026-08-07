#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{

// ============================================================================
// AABB (Axis-Aligned Bounding Box)
// ============================================================================
// Broad Phaseで「衝突する可能性があるか」を安価に調べるための境界箱です。
// Min / Maxは常にワールド座標で保持します。
//
// AABBは回転を持たないため、判定は各軸の区間が重なっているかを見るだけです。
// Narrow Phaseの正確な形状判定より非常に軽いため、先にAABBで候補を絞ります。
struct AABB
{
    math::Vec3 Min{};
    math::Vec3 Max{};

    // 3軸すべてで区間が重なっていればAABB同士は重なっています。
    // 境界がちょうど接する場合もNarrow Phaseへ渡したいため <= / >= を使います。
    bool Overlaps(const AABB& other) const;
};

// Sphere / Box ColliderからBroad Phase用AABBを生成します。
// 無限Planeは有限のAABBを定義できないためfalseを返し、別経路で処理します。
bool ComputeColliderAABB(
    const TransformComponent& transform,
    const ColliderComponent& collider,
    AABB& outAABB);

} // namespace Raven::ph
