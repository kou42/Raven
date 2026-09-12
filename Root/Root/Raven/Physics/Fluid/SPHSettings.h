#pragma once

#include <cstdint>

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
// Stable Time StepではFrame deltaTimeを必要に応じて複数Substepへ分割し、
// 明示積分が1回で進み過ぎることを抑えます。
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

    // Stable Time Stepを有効にすると、Frame deltaTimeをCFL系の上限で分割します。
    bool StableTimeStepEnabled = true;

    // dt_velocity = CFLFactor * h / max(|v|, c) の安全係数です。
    // 1未満にしてParticleがsupport radiusを1 Substepで大きく飛び越えないようにします。
    float CFLFactor = 0.4f;

    // 線形EOSのPressureStiffnessから推定する数値的な音速 c = sqrt(k) に掛ける係数です。
    // 剛性が高いほど圧力波が速く伝わるため、時間刻みを小さくするために使用します。
    float SpeedOfSoundScale = 1.0f;

    // 加速度による移動量にも制約を掛けます。
    // dt_acceleration = AccelerationTimeStepFactor * sqrt(h / max(|a|))
    float AccelerationTimeStepFactor = 0.25f;

    // Substepが極端に細かくなりCPU時間が暴走することを防ぐ下限です。
    float MinimumTimeStep = 1.0e-5f;

    // 1 Frameで許可するSubstep数の上限です。
    uint32_t MaximumSubsteps = 16u;

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
