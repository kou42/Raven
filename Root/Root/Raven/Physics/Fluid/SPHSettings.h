#pragma once

namespace Raven
{
namespace ph
{

// ============================================================================
// SPH Settings
// ============================================================================
// 最初のSPH実装ではDensity / Pressureを理解しやすく分離します。
// Force / Integration / Boundaryは次段階で追加し、Solverの責務を段階的に広げます。
struct SPHSettings
{
    // Kernelのsupport radius hです。Spatial HashのCellSizeにも同じ値を使用します。
    float SmoothingRadius = 0.1f;

    // Equation of Stateが目標とする静止密度 rho_0 です。
    float RestDensity = 1000.0f;

    // p = k * (rho - rho_0) の剛性係数 k です。
    float PressureStiffness = 200.0f;
};

} // namespace ph
} // namespace Raven
