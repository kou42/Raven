#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// Fluid Particle
// ============================================================================
// SPH / PBFなどParticle-based Fluidで共通利用する最小Particle状態です。
// SoftBodyParticleとは物理意味が異なるため無理に共通Base型へ統合せず、Spatial Queryなど
// 本当に共有できる機構だけをParticle Coreへ切り出します。
struct FluidParticle
{
    math::Vec3 Position{};
    math::Vec3 Velocity{};
    math::Vec3 Force{};

    float Density = 0.0f;
    float Pressure = 0.0f;
    float Mass = 1.0f;
};

} // namespace ph
} // namespace Raven
