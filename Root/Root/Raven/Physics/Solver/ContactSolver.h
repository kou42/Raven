#pragma once

#include "Raven/Physics/Contact.h"

namespace Raven
{

class Scene;

namespace ph
{

// ============================================================================
// ContactManifold Solver
// ============================================================================
// Manifoldに記録された法線・Material情報と、各ContactPointの位置・貫通量を使って
// 位置と速度を補正します。
//
// 現在はManifold内部の各点を順番に解決する最小構成です。
// 将来のSequential Impulse化では次の3段階へ発展させます。
//   1. Manifold全体のConstraintを事前計算する
//   2. 全接触点へWarm Startを適用する
//   3. 全Manifoldを複数回反復してImpulseを収束させる
void SolveContactManifold(
    Scene& scene,
    ContactManifold& manifold,
    float dt);

} // namespace ph

} // namespace Raven
