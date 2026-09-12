#pragma once

namespace Raven
{
namespace ph
{

// ============================================================================
// SPH Kernel Functions
// ============================================================================
// 数式とSolver処理を分離し、今後Spiky Gradient / Viscosity Laplacianを同じ場所へ追加します。
class SPHKernel
{
public:
    // 3D Poly6 Kernel:
    // W(r, h) = 315 / (64 * pi * h^9) * (h^2 - r^2)^3, 0 <= r <= h
    static float EvaluatePoly6Density(float distanceSq, float smoothingRadius);
};

} // namespace ph
} // namespace Raven
