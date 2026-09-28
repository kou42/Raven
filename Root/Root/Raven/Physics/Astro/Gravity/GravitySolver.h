#pragma once

#include <vector>

#include "Raven/Physics/Astro/AstroStatistics.h"
#include "Raven/Physics/Astro/AstroVector.h"
#include "Raven/Scene/Entity.h"

namespace Raven::ph
{

struct AstroBodyState
{
    EntityHandle Entity{};
    AstroVector3 Position{};
    AstroVector3 Velocity{};
    double Mass = 0.0;
    bool GenerateGravity = true;
    bool ReceiveGravity = true;
};

struct GravitySolverSettings
{
    // SI単位系を基準とするNewton重力定数です。
    // Ravenの通常Sceneで別スケールを使う場合はAstroWorld経由で明示的に変更します。
    double GravitationalConstant = 6.67430e-11;
    double MinimumDistance = 0.01;
};

class GravitySolver
{
public:
    virtual ~GravitySolver() = default;

    virtual void ComputeForces(
        const std::vector<AstroBodyState>& bodies,
        const GravitySolverSettings& settings,
        std::vector<AstroVector3>& outForces,
        AstroStatistics* statistics = nullptr) const = 0;
};

} // namespace Raven::ph
