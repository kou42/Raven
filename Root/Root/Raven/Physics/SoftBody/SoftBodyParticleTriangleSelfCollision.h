#pragma once

#include <cstdint>

namespace Raven
{
namespace ph
{

class SoftBodySolver;
struct SoftBodyCloth;

// ============================================================================
// Cloth Particle-Triangle Self Collision Settings
// ============================================================================
struct SoftBodyParticleTriangleSelfCollisionSettings
{
    bool Enabled = true;

    // Particle中心とTriangle表面の間に維持する最小距離です。
    // Clothを数学的な厚み0の面として扱いつつ、自己貫通を防ぐための半厚みに相当します。
    float Thickness = 0.01f;

    // ========================================================================
    // Spatial Hash Cell Size Comparison Presets
    // ========================================================================
    // Narrow Phase最適化へ進む前に、Broad PhaseのCell Sizeを
    // 0.04 / 0.05 / 0.06で比較するための共通プリセットです。
    // Deformer・SelfTest・Editor/Profiler側が同じ値を参照できるようSettingsへ集約します。
    static constexpr float SpatialHashCellSizeSmall = 0.04f;
    static constexpr float SpatialHashCellSizeMedium = 0.05f;
    static constexpr float SpatialHashCellSizeLarge = 0.06f;

    // Particle-Triangle Broad Phase専用のSpatial Hash Cell Sizeです。
    // Thicknessとは独立した値です。
    // Cellを小さくすると1 Cellあたりの候補Triangleは減りますが、
    // Triangleが跨ぐCell数が増えてHashBuildが重くなります。
    // 大きくするとHashBuildは軽くなりますが、
    // Candidate/Narrow Phaseの候補数が増加します。
    float SpatialHashCellSize = SpatialHashCellSizeMedium;

    // Particle-Triangle自己衝突専用の追加反復回数です。
    uint32_t SolverIterations = 4u;

    // XPBD Compliance。0.0fなら硬い自己衝突です。
    float Compliance = 0.0f;
};

// ============================================================================
// Solve Cloth Particle-Triangle Self Collisions
// ============================================================================
// Cloth grid topologyからTriangleを復元し、Spatial Hash Broad Phaseで候補を絞った後、
// ParticleとTriangle最近傍点の距離ConstraintをXPBDで解きます。
//
// 自分自身を頂点として含むTriangleは必ず除外します。
// さらにTriangle補正は最近傍点のBarycentric Weightで3頂点へ分配するため、
// Particleだけを一方的に押すのではなくCloth同士の接触として双方へ反作用が伝わります。
void SolveSoftBodyParticleTriangleSelfCollisions(
    SoftBodySolver& solver,
    const SoftBodyCloth& cloth,
    float deltaTime,
    const SoftBodyParticleTriangleSelfCollisionSettings& settings);

} // namespace ph
} // namespace Raven
