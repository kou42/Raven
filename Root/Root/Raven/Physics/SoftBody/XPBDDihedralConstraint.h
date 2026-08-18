#pragma once

#include <cstdint>

namespace Raven
{
namespace ph
{

// ============================================================================
// XPBD Dihedral Bending Constraint
// ============================================================================
// 1本の共有Edgeを持つ2枚のTriangle間の二面角を拘束するためのConstraintです。
//
// Topology:
//
//          OppositeA (p0)
//             / \
//            /   \
//       EdgeA-----EdgeB   <- shared edge (p2-p3)
//            \   /
//             \ /
//          OppositeB (p1)
//
// RestAngleはConstraint登録時の二面角[rad]です。
// 平らなClothでは0付近になり、折れ曲がると現在角との差がConstraint Errorになります。
//
// LambdaはDistance Constraintと同じくXPBDのLagrange multiplierです。
// 同一Step内のSolver iteration間では蓄積し、Step開始時に0へ戻します。
struct XPBDDihedralConstraint
{
    uint32_t OppositeA = 0u;
    uint32_t OppositeB = 0u;
    uint32_t EdgeA = 0u;
    uint32_t EdgeB = 0u;

    float RestAngle = 0.0f;
    float Compliance = 0.0f;
    float Lambda = 0.0f;
};

} // namespace ph
} // namespace Raven
