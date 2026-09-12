#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// SPH Kernel Functions
// ============================================================================
// Density・Pressure・Viscosityで使用する3D Kernelをここへ集約し、Solver側には
// 「どの物理項で使うか」だけが残るようにします。
class SPHKernel
{
public:
    // 3D Poly6 Kernel:
    // W(r, h) = 315 / (64 * pi * h^9) * (h^2 - r^2)^3, 0 <= r <= h
    static float EvaluatePoly6Density(float distanceSq, float smoothingRadius);

    // 3D Spiky Kernel Gradient:
    // grad W = -45 / (pi * h^6) * (h-r)^2 * rHat, 0 < r <= h
    // displacementは評価Particle iからNeighbor jへ向かう差 ri-rj とします。
    static math::Vec3 EvaluateSpikyGradient(
        const math::Vec3& displacement,
        float distance,
        float smoothingRadius);

    // 3D Viscosity Kernel Laplacian:
    // laplacian W = 45 / (pi * h^6) * (h-r), 0 <= r <= h
    static float EvaluateViscosityLaplacian(float distance, float smoothingRadius);
};

} // namespace ph
} // namespace Raven
