#include "Raven/Physics/Coupling/FluidRigidBodyCoupling.h"

#include <algorithm>
#include <cmath>

#include "Raven/Core/CPUProfiler.h"
#include "Raven/Math/Math.h"
#include "Raven/Physics/Coupling/FluidColliderContact.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/RigidBodyDynamics.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
constexpr float MinimumParticleRadius = 0.0f;

float ComputeDirectionalEffectiveInverseMass(
    float particleInverseMass,
    float bodyInverseMass,
    const math::Mat3& bodyInverseInertia,
    const math::Vec3& leverArm,
    const math::Vec3& direction)
{
    // 任意方向のImpulseに対するRigidBodyの回転応答を有効質量へ変換します。
    // NormalとDragで同じ式を使うことで、重心から外れた接触の回転しやすさを一貫して扱います。
    const math::Vec3 angularResponse = bodyInverseInertia
        * math::Vec3::Cross(leverArm, direction);
    const float rotationalInverseMass = math::Vec3::Dot(
        math::Vec3::Cross(angularResponse, leverArm),
        direction);
    return particleInverseMass
        + bodyInverseMass
        + std::max(0.0f, rotationalInverseMass);
}
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
    m_Settings.DragCoefficient = std::clamp(settings.DragCoefficient, 0.0f, 1.0f);
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
    profiler.AddCounter("Physics.Fluid.RigidBody.DynamicBodyCount", static_cast<double>(m_LastStatistics.DynamicBodyCount));
    profiler.AddCounter("Physics.Fluid.RigidBody.CandidatePairCount", static_cast<double>(m_LastStatistics.CandidatePairCount));
    profiler.AddCounter("Physics.Fluid.RigidBody.ResolvedContactCount", static_cast<double>(m_LastStatistics.ResolvedContactCount));
    profiler.AddCounter("Physics.Fluid.RigidBody.AppliedImpulseCount", static_cast<double>(m_LastStatistics.AppliedImpulseCount));
    profiler.AddCounter("Physics.Fluid.RigidBody.AppliedDragImpulseCount", static_cast<double>(m_LastStatistics.AppliedDragImpulseCount));
    profiler.AddCounter("Physics.Fluid.RigidBody.TotalNormalImpulse", static_cast<double>(m_LastStatistics.TotalNormalImpulse));
    profiler.AddCounter("Physics.Fluid.RigidBody.TotalDragImpulse", static_cast<double>(m_LastStatistics.TotalDragImpulse));
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
    if (GenerateFluidParticleColliderContact(particle, m_Settings.ParticleRadius, transform, collider, contact) == false)
    {
        return false;
    }

    particle.Position = contact.CorrectedParticlePosition;

    const float particleMass = std::max(0.0f, particle.Mass);
    if (particleMass <= math::Epsilon)
    {
        return true;
    }

    const float particleInverseMass = 1.0f / particleMass;
    const math::Vec3 leverArm = contact.Point - transform.Position;
    const math::Mat3 bodyInverseInertia = ComputeWorldInverseInertia(&transform, &rigidBody, &collider);

    // ------------------------------------------------------------------------
    // Normal collision impulse
    // ------------------------------------------------------------------------
    math::Vec3 bodyPointVelocity = rigidBody.LinearVelocity
        + math::Vec3::Cross(rigidBody.AngularVelocity, leverArm);
    math::Vec3 relativeVelocity = particle.Velocity - bodyPointVelocity;
    const float relativeNormalVelocity = math::Vec3::Dot(relativeVelocity, contact.Normal);

    if (relativeNormalVelocity < 0.0f)
    {
        const float normalEffectiveInverseMass = ComputeDirectionalEffectiveInverseMass(
            particleInverseMass,
            rigidBody.InverseMass,
            bodyInverseInertia,
            leverArm,
            contact.Normal);
        if (normalEffectiveInverseMass > math::Epsilon)
        {
            const float restitution = std::min(
                m_Settings.Restitution,
                std::clamp(collider.Restitution, 0.0f, 1.0f));
            const float impulseMagnitude =
                -(1.0f + restitution) * relativeNormalVelocity / normalEffectiveInverseMass;

            if (impulseMagnitude > 0.0f)
            {
                const math::Vec3 impulseOnParticle = contact.Normal * impulseMagnitude;
                particle.Velocity += impulseOnParticle * particleInverseMass;
                physicsWorld.AddImpulseAtPoint(scene, entity, -impulseOnParticle, contact.Point);

                ++m_LastStatistics.AppliedImpulseCount;
                m_LastStatistics.TotalNormalImpulse += impulseMagnitude;
            }
        }
    }

    // ------------------------------------------------------------------------
    // Tangential drag impulse
    // ------------------------------------------------------------------------
    // Normal impulse適用後の速度から接線成分を再計算します。
    // Dragは法線方向の反発量を変えず、FluidとRigidBody表面の滑りだけを減衰させます。
    if (m_Settings.DragCoefficient > 0.0f)
    {
        bodyPointVelocity = rigidBody.LinearVelocity
            + math::Vec3::Cross(rigidBody.AngularVelocity, leverArm);
        relativeVelocity = particle.Velocity - bodyPointVelocity;
        const math::Vec3 tangentialVelocity = relativeVelocity
            - contact.Normal * math::Vec3::Dot(relativeVelocity, contact.Normal);
        const float tangentialSpeedSq = tangentialVelocity.LengthSq();

        if (tangentialSpeedSq > math::Epsilon * math::Epsilon)
        {
            const float tangentialSpeed = std::sqrt(tangentialSpeedSq);
            const math::Vec3 tangent = tangentialVelocity / tangentialSpeed;
            const float dragEffectiveInverseMass = ComputeDirectionalEffectiveInverseMass(
                particleInverseMass,
                rigidBody.InverseMass,
                bodyInverseInertia,
                leverArm,
                tangent);

            if (dragEffectiveInverseMass > math::Epsilon)
            {
                // 相対接線速度を0へ近づけるImpulseへDragCoefficientを掛けます。
                // 係数を[0,1]へ制限することで、1回の解決で相対速度を反転させません。
                const float dragImpulseMagnitude = m_Settings.DragCoefficient
                    * tangentialSpeed / dragEffectiveInverseMass;
                const math::Vec3 dragImpulseOnParticle = -tangent * dragImpulseMagnitude;

                particle.Velocity += dragImpulseOnParticle * particleInverseMass;
                physicsWorld.AddImpulseAtPoint(scene, entity, -dragImpulseOnParticle, contact.Point);

                ++m_LastStatistics.AppliedDragImpulseCount;
                m_LastStatistics.TotalDragImpulse += dragImpulseMagnitude;
            }
        }
    }

    return true;
}

} // namespace Raven::ph
