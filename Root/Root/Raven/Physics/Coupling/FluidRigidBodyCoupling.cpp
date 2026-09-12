#include "Raven/Physics/Coupling/FluidRigidBodyCoupling.h"

#include <algorithm>

#include "Raven/Core/CPUProfiler.h"
#include "Raven/Math/Math.h"
#include "Raven/Physics/Coupling/FluidColliderContact.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
constexpr float MinimumParticleRadius = 0.0f;
}

FluidRigidBodyCoupling::FluidRigidBodyCoupling(
    const FluidRigidBodyCouplingSettings& settings)
{
    SetSettings(settings);
}

void FluidRigidBodyCoupling::SetSettings(
    const FluidRigidBodyCouplingSettings& settings)
{
    m_Settings = settings;
    m_Settings.ParticleRadius = std::max(MinimumParticleRadius, settings.ParticleRadius);
    m_Settings.Restitution = std::clamp(settings.Restitution, 0.0f, 1.0f);
}

void FluidRigidBodyCoupling::ResolveScene(
    Scene& scene,
    PhysicsWorld& physicsWorld,
    std::vector<FluidParticle>& particles)
{
    RAVEN_PROFILE_SCOPE("Physics.Fluid.RigidBodyCoupling");
    m_LastStatistics = {};

    if (particles.empty())
    {
        return;
    }

    for (auto [entity, transform, rigidBody, collider]
        : scene.View<TransformComponent, RigidBodyComponent, ColliderComponent>())
    {
        if (rigidBody.Type != BodyType::Dynamic || rigidBody.InverseMass <= 0.0f)
        {
            continue;
        }
        if (collider.IsTrigger)
        {
            continue;
        }
        if (collider.Type != ColliderType::Sphere && collider.Type != ColliderType::Box)
        {
            continue;
        }

        ++m_LastStatistics.DynamicBodyCount;
        for (FluidParticle& particle : particles)
        {
            ++m_LastStatistics.CandidatePairCount;
            if (ResolveParticleAgainstRigidBody(
                scene,
                physicsWorld,
                entity,
                particle,
                transform,
                rigidBody,
                collider))
            {
                ++m_LastStatistics.ResolvedContactCount;
            }
        }
    }

    CPUProfiler& profiler = CPUProfiler::Get();
    profiler.AddCounter(
        "Physics.Fluid.RigidBody.DynamicBodyCount",
        static_cast<double>(m_LastStatistics.DynamicBodyCount));
    profiler.AddCounter(
        "Physics.Fluid.RigidBody.CandidatePairCount",
        static_cast<double>(m_LastStatistics.CandidatePairCount));
    profiler.AddCounter(
        "Physics.Fluid.RigidBody.ResolvedContactCount",
        static_cast<double>(m_LastStatistics.ResolvedContactCount));
    profiler.AddCounter(
        "Physics.Fluid.RigidBody.AppliedImpulseCount",
        static_cast<double>(m_LastStatistics.AppliedImpulseCount));
    profiler.AddCounter(
        "Physics.Fluid.RigidBody.TotalNormalImpulse",
        static_cast<double>(m_LastStatistics.TotalNormalImpulse));
}

bool FluidRigidBodyCoupling::ResolveParticleAgainstRigidBody(
    Scene& scene,
    PhysicsWorld& physicsWorld,
    Entity entity,
    FluidParticle& particle,
    const TransformComponent& transform,
    RigidBodyComponent& rigidBody,
    const ColliderComponent& collider)
{
    if (rigidBody.Type != BodyType::Dynamic || rigidBody.InverseMass <= 0.0f)
    {
        return false;
    }

    FluidColliderContact contact{};
    if (GenerateFluidParticleColliderContact(
        particle,
        m_Settings.ParticleRadius,
        transform,
        collider,
        contact) == false)
    {
        return false;
    }

    // Penetration correctionはParticle側へ適用します。
    // RigidBodyのTransformを直接移動するとPhysicsWorldのContact Solverと競合するため、
    // Body側の反応はImpulseだけに限定します。
    particle.Position = contact.CorrectedParticlePosition;

    const float particleMass = std::max(0.0f, particle.Mass);
    if (particleMass <= math::Epsilon)
    {
        return true;
    }

    const math::Vec3 leverArm = contact.Point - transform.Position;
    const math::Vec3 bodyPointVelocity = rigidBody.LinearVelocity
        + math::Vec3::Cross(rigidBody.AngularVelocity, leverArm);
    const math::Vec3 relativeVelocity = particle.Velocity - bodyPointVelocity;
    const float relativeNormalVelocity = math::Vec3::Dot(relativeVelocity, contact.Normal);
    if (relativeNormalVelocity >= 0.0f)
    {
        return true;
    }

    const float particleInverseMass = 1.0f / particleMass;
    const float effectiveInverseMass = particleInverseMass + rigidBody.InverseMass;
    if (effectiveInverseMass <= math::Epsilon)
    {
        return true;
    }

    const float restitution = std::min(
        m_Settings.Restitution,
        std::clamp(collider.Restitution, 0.0f, 1.0f));

    // J = -(1+e) v_rel,n / (m_p^-1 + m_b^-1)
    // 現段階では回転有効質量を分母へ含めません。Impulse作用点は既存PhysicsWorldへ渡すため、
    // 得られた反作用はLinearVelocityだけでなくr x Jを通じてAngularVelocityにも反映されます。
    const float impulseMagnitude =
        -(1.0f + restitution) * relativeNormalVelocity / effectiveInverseMass;
    if (impulseMagnitude <= 0.0f)
    {
        return true;
    }

    const math::Vec3 impulseOnParticle = contact.Normal * impulseMagnitude;
    particle.Velocity += impulseOnParticle * particleInverseMass;

    // Newtonの第三法則に従い、RigidBodyへ等量反対向きのImpulseを返します。
    physicsWorld.AddImpulseAtPoint(
        scene,
        entity,
        -impulseOnParticle,
        contact.Point);

    ++m_LastStatistics.AppliedImpulseCount;
    m_LastStatistics.TotalNormalImpulse += impulseMagnitude;
    return true;
}

} // namespace Raven::ph
