#include "Raven/Physics/Astro/AstroWorld.h"

#include <cstddef>

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
        state.Position = transform.Position;
        state.Mass = static_cast<double>(rigidBody.Mass);
        state.GenerateGravity = celestialBody.GenerateGravity;
        state.ReceiveGravity = celestialBody.ReceiveGravity;
        m_Bodies.push_back(state);
    }

    m_GravitySolver->ComputeForces(m_Bodies, m_Settings, m_Forces);
    if (m_Forces.size() != m_Bodies.size())
    {
        // Solver境界違反時に部分的なForceだけをSceneへ反映しないよう、step全体を破棄します。
        return;
    }

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

        const math::Vec3& force = m_Forces[i];
        rigidBody->Force += force;
        if (force.LengthSq() > 0.0f)
        {
            WakeRigidBody(*rigidBody);
        }
    }
}

void AstroWorld::Clear()
{
    m_Bodies.clear();
    m_Forces.clear();
}

void AstroWorld::SetGravitySolver(GravitySolver* solver)
{
    // nullptrは「Direct Solverへ戻す」と解釈し、Worldを無効状態へ残しません。
    m_GravitySolver = solver != nullptr ? solver : &m_DirectGravitySolver;
}

} // namespace Raven::ph
