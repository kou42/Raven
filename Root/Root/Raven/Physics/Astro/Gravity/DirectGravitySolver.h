#pragma once

#include "Raven/Physics/Astro/Gravity/GravitySolver.h"

namespace Raven::ph
{

// O(N^2)で全ペアを一度ずつ評価するReference Solverです。
// Barnes-Hut等の近似Solverは将来この結果との誤差比較を行います。
class DirectGravitySolver final : public GravitySolver
{
public:
    void ComputeForces(
        const std::vector<AstroBodyState>& bodies,
        const GravitySolverSettings& settings,
        std::vector<AstroVector3>& outForces) const override;
};

} // namespace Raven::ph
