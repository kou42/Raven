#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// SPH Settings
// ============================================================================
// Density / Pressureだけでなく、Pressure Force・Viscosity・Gravity・Integration・
// Box Boundaryまで1 Stepで追える最小SPH設定です。
// 高度な安定化（CFL/Substep/PBF）は別段階とし、まず各物理項の意味がコードから追える構成を優先します。
struct SPHSettings
{
    // Kernelのsupport radius hです。Spatial HashのCellSizeにも同じ値を使用します。
    float SmoothingRadius = 0.1f;

    // Equation of Stateが目標とする静止密度 rho_0 です。
    float RestDensity = 1000.0f;

    // p = k * (rho - rho_0) の剛性係数 k です。
    float PressureStiffness = 200.0f;

    // SPH粘性項の係数です。0なら粘性力を無効化します。
    float Viscosity = 0.1f;

    // 外力として各Particleへ m*g を加えます。
    math::Vec3 Gravity{ 0.0f, -9.81f, 0.0f };

    // 最初の境界条件は軸平行Boxです。
    // falseならBoundary処理を完全にスキップします。
    bool BoundaryEnabled = false;
    math::Vec3 BoundaryMinimum{ -1.0f, -1.0f, -1.0f };
    math::Vec3 BoundaryMaximum{ 1.0f, 1.0f, 1.0f };

    // Particle中心をBox面からこの距離だけ内側へ保ちます。
    float BoundaryParticleRadius = 0.02f;

    // 壁法線方向速度の反発係数です。0なら非弾性、1なら完全反射です。
    float BoundaryRestitution = 0.0f;
};

} // namespace ph
} // namespace Raven
