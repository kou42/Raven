#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Physics/Contact.h"

namespace Raven
{

class Scene;

namespace ph
{

// ============================================================================
// ContactSolverSettings
// ============================================================================
// Rotation/Angular Impulse導入前の線形Solver設定です。
// VelocityIterationsを増やすほど、複数Manifoldが連鎖する積み重ね・挟み込みで
// Impulseが伝播しやすくなります。
struct ContactSolverSettings
{
    uint32_t VelocityIterations = 8;

    // 位置補正は速度Impulseとは分離し、Manifoldごとに1回だけ適用します。
    // Box-Boxの4点Manifoldで4倍補正される問題を防ぎます。
    float PenetrationSlop = 0.001f;
    float PositionCorrectionPercent = 0.8f;

    // ごく小さい衝突速度では反発を無効化し、静止物体の細かなバウンドを抑えます。
    float RestitutionVelocityThreshold = 0.5f;
};

// ============================================================================
// SolveContactManifolds
// ============================================================================
// 1. 各Manifoldに位置補正を1回だけ適用
// 2. 全ManifoldをVelocityIterations回反復
// 3. 法線Impulseを累積し、0未満にならないようClamp
// 4. 摩擦Impulseを累積し、Coulomb限界内へClamp
//
// 現在はAngularVelocity/慣性テンソルを衝突応答へまだ含めないため、Box-Boxの
// 複数ContactPointは「接触面情報」として保持しつつ、線形ImpulseはManifold単位で
// 解きます。将来Angular Impulseを導入した時点で各ContactPoint固有の
// rA/rBを使う完全なSequential Impulseへ移行します。
void SolveContactManifolds(
    Scene& scene,
    std::vector<ContactManifold>& manifolds,
    float dt,
    const ContactSolverSettings& settings = ContactSolverSettings{});

// 単一Manifold用互換APIです。
void SolveContactManifold(
    Scene& scene,
    ContactManifold& manifold,
    float dt);

} // namespace ph

} // namespace Raven
