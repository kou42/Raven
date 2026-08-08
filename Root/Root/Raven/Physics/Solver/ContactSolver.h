#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Physics/Contact.h"

namespace Raven
{
class Scene;

namespace ph
{

struct ContactSolverSettings
{
    uint32_t VelocityIterations = 8;

    // Position Solverも反復させます。Velocity Solverとは分離し、速度へ人工的な
    // エネルギーを注入せずにpenetrationだけを解消します。
    uint32_t PositionIterations = 3;
    float PenetrationSlop = 0.001f;
    float PositionCorrectionPercent = 0.8f;

    float RestitutionVelocityThreshold = 0.5f;

    // Contact Persistenceで前Stepから引き継いだ累積Impulseを、反復Solver開始前に
    // 初期解としてBodyへ適用します。積み重ね・静止接触の収束を高速化します。
    bool EnableWarmStart = true;
};

void SolveContactManifolds(
    Scene& scene,
    std::vector<ContactManifold>& manifolds,
    float dt,
    const ContactSolverSettings& settings = ContactSolverSettings{});

void SolveContactManifold(
    Scene& scene,
    ContactManifold& manifold,
    float dt);

} // namespace ph
} // namespace Raven
