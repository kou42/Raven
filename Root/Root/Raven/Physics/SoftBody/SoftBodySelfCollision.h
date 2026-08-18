#pragma once

#include <cstdint>

namespace Raven
{
namespace ph
{

class SoftBodySolver;

// ============================================================================
// Soft Body Particle Self Collision Settings
// ============================================================================
// Cloth / SoftBody の Particle-Particle 自己衝突を制御する設定です。
// Broad Phase は SoftBodySpatialHashGrid を使用し、Narrow Phase では各 Particle を
// 半径 ParticleRadius の Sphere とみなして重なりを解消します。
struct SoftBodySelfCollisionSettings
{
    bool Enabled = true;

    // Particle を自己衝突判定用の Sphere とみなしたときの半径です。
    // 2 * ParticleRadius が自己衝突で維持する最小中心間距離になります。
    float ParticleRadius = 0.01f;

    // 自己衝突だけを追加反復する回数です。
    // 現段階では既存 SoftBodySolver の内部 Constraint を変更せず、安全に段階導入するため
    // Solver::Step() 後に独立した Position Constraint pass として反復します。
    uint32_t SolverIterations = 4u;

    // XPBD Compliance。0.0f なら硬い自己衝突、値を大きくすると柔らかくなります。
    float Compliance = 0.0f;
};

// ============================================================================
// Solve Soft Body Particle Self Collisions
// ============================================================================
// Spatial Hash / Uniform Grid で候補 Pair を絞り込み、Particle-Particle の重なりを解消します。
//
// 重要:
// - Distance Constraint で直接接続されている Particle Pair は除外します。
//   Structural / Shear 等の Cloth Topology 自身が自己衝突で押し広げられることを防ぎます。
// - 各自己衝突 iteration の直前に Spatial Hash を再構築します。
//   Position 補正で Particle がセル境界を跨いでも、次 iteration では最新位置を使用します。
// - Position 補正後に PreviousPosition との差から Velocity を再構築します。
//   Solver::Step() 後の追加 pass でも、自己衝突による移動量を次 Step の運動へ引き継げます。
void SolveSoftBodyParticleSelfCollisions(
    SoftBodySolver& solver,
    float deltaTime,
    const SoftBodySelfCollisionSettings& settings);

} // namespace ph
} // namespace Raven
