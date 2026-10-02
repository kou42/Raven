#include "Raven/Physics/Astro/AstroWorld.h"

#include <chrono>
#include <cstddef>
#include <cmath>

#include "Raven/Physics/Astro/CelestialBody.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
bool CanReceiveGravity(const RigidBodyComponent& rigidBody)
{
    return rigidBody.Type == BodyType::Dynamic && rigidBody.InverseMass > 0.0f;
}

void WakeRigidBody(RigidBodyComponent& rigidBody)
{
    rigidBody.IsSleeping = false;
    rigidBody.SleepTimer = 0.0f;
}
}

void AstroWorld::AccumulateGravityForces(Scene& scene, float fixedDeltaTime)
{
    static_cast<void>(fixedDeltaTime);

    m_Bodies.clear();
    m_Forces.clear();
    m_Statistics.Clear();

    using Clock = std::chrono::steady_clock;
    const auto collectionBegin = Clock::now();

    if (m_GravitySolver == nullptr)
    {
        return;
    }

    for (auto [entity, transform, rigidBody, celestialBody]
        : scene.View<TransformComponent, RigidBodyComponent, CelestialBodyComponent>())
    {
        if (celestialBody.GenerateGravity == false && celestialBody.ReceiveGravity == false)
        {
            continue;
        }

        AstroBodyState state{};
        state.Entity = entity;
        state.Position = AstroVector3(transform.Position);
        state.Velocity = AstroVector3(rigidBody.LinearVelocity);
        state.Mass = static_cast<double>(rigidBody.Mass);
        state.GenerateGravity = celestialBody.GenerateGravity;
        state.ReceiveGravity = celestialBody.ReceiveGravity;
        m_Bodies.push_back(state);
    }

    const auto collectionEnd = Clock::now();
    m_Statistics.ActiveBodyCount = static_cast<std::uint64_t>(m_Bodies.size());
    m_Statistics.StateCollectionTimeMs =
        std::chrono::duration<double, std::milli>(collectionEnd - collectionBegin).count();

    // 診断値はSolver実行前の同一snapshotから計算し、Force計算との時刻ずれを避けます。
    // 診断計算はGravity Solverの性能値へ混ぜず、Solver単体の増加傾向を追えるようにします。
    m_LastDiagnostics = OrbitalDiagnosticsCalculator::Compute(m_Bodies, m_Settings);
    const auto solveBegin = Clock::now();
    m_GravitySolver->ComputeForces(m_Bodies, m_Settings, m_Forces, &m_Statistics);
    const auto solveEnd = Clock::now();
    m_Statistics.GravitySolveTimeMs =
        std::chrono::duration<double, std::milli>(solveEnd - solveBegin).count();
    if (m_Forces.size() != m_Bodies.size())
    {
        // Solver境界違反時に部分的なForceだけをSceneへ反映しないよう、step全体を破棄します。
        return;
    }

    const auto feedbackBegin = Clock::now();
    for (std::size_t i = 0u; i < m_Bodies.size(); ++i)
    {
        const AstroBodyState& state = m_Bodies[i];
        if (state.ReceiveGravity == false || scene.IsEntityAlive(state.Entity) == false)
        {
            continue;
        }

        RigidBodyComponent* rigidBody =
            scene.TryGetComponent<RigidBodyComponent>(state.Entity.m_Index);
        if (rigidBody == nullptr || CanReceiveGravity(*rigidBody) == false)
        {
            continue;
        }

        const AstroVector3& astroForce = m_Forces[i];
        const math::Vec3 force = astroForce.ToSceneVector();
        if (std::isfinite(force.x) == false
            || std::isfinite(force.y) == false
            || std::isfinite(force.z) == false)
        {
            // double側で有限でもfloat Scene境界への縮小変換でoverflowする可能性があります。
            // 非有限値を既存Rigid Force accumulatorへ流さないことを境界で保証します。
            continue;
        }
        rigidBody->Force += force;
        if (astroForce.LengthSq() > 0.0)
        {
            WakeRigidBody(*rigidBody);
        }
    }
    const auto feedbackEnd = Clock::now();
    m_Statistics.ForceFeedbackTimeMs =
        std::chrono::duration<double, std::milli>(feedbackEnd - feedbackBegin).count();
}

void AstroWorld::Clear()
{
    m_Bodies.clear();
    m_Forces.clear();
    m_LastDiagnostics = OrbitalDiagnostics{};
    m_Statistics.Clear();
}

void AstroWorld::SetGravitySolver(GravitySolver* solver)
{
    // nullptrは「Direct Solverへ戻す」と解釈し、Worldを無効状態へ残しません。
    m_GravitySolver = solver != nullptr ? solver : &m_DirectGravitySolver;
}

} // namespace Raven::ph
