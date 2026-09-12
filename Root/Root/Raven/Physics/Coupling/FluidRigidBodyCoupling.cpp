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
constexpr float Pi = 3.14159265358979323846f;

float ComputeDirectionalEffectiveInverseMass(
    float particleInverseMass,
    float bodyInverseMass,
    const math::Mat3& bodyInverseInertia,
    const math::Vec3& leverArm,
    const math::Vec3& direction)
{
    const math::Vec3 angularResponse = bodyInverseInertia
        * math::Vec3::Cross(leverArm, direction);
    const float rotationalInverseMass = math::Vec3::Dot(
        math::Vec3::Cross(angularResponse, leverArm),
        direction);
    return particleInverseMass
        + bodyInverseMass
        + std::max(0.0f, rotationalInverseMass);
}

float ComputeDisplacedFluidFraction(float penetrationDepth, float particleRadius)
{
    if (particleRadius <= math::Epsilon)
    {
        return 0.0f;
    }

    // Collider表面を局所平面とみなし、Particle Sphereが表面へどれだけ入り込んだかを
    // 0..1の排除率へ変換します。接触開始で0、Particle中心が表面上で0.5、
    // 中心が半径分Collider内部へ入った状態で1となるため、部分浸水を連続的に扱えます。
    return std::clamp(penetrationDepth / (2.0f * particleRadius), 0.0f, 1.0f);
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
    m_Settings.PressureReactionCoefficient = std::max(0.0f, settings.PressureReactionCoefficient);
    m_Settings.BuoyancyCoefficient = std::max(0.0f, settings.BuoyancyCoefficient);
}

void FluidRigidBodyCoupling::ResolveScene(
    Scene& scene,
    PhysicsWorld& physicsWorld,
    std::vector<FluidParticle>& particles,
    float deltaTime)
{
    RAVEN_PROFILE_SCOPE("Physics.Fluid.RigidBodyCoupling");
    m_LastStatistics = {};

    if (particles.empty())
    {
        return;
    }

    const float safeDeltaTime = std::max(0.0f, deltaTime);

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
                collider,
                safeDeltaTime))
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
    profiler.AddCounter("Physics.Fluid.RigidBody.AppliedPressureImpulseCount", static_cast<double>(m_LastStatistics.AppliedPressureImpulseCount));
    profiler.AddCounter("Physics.Fluid.RigidBody.AppliedBuoyancyImpulseCount", static_cast<double>(m_LastStatistics.AppliedBuoyancyImpulseCount));
    profiler.AddCounter("Physics.Fluid.RigidBody.TotalNormalImpulse", static_cast<double>(m_LastStatistics.TotalNormalImpulse));
    profiler.AddCounter("Physics.Fluid.RigidBody.TotalDragImpulse", static_cast<double>(m_LastStatistics.TotalDragImpulse));
    profiler.AddCounter("Physics.Fluid.RigidBody.TotalPressureImpulse", static_cast<double>(m_LastStatistics.TotalPressureImpulse));
    profiler.AddCounter("Physics.Fluid.RigidBody.TotalBuoyancyImpulse", static_cast<double>(m_LastStatistics.TotalBuoyancyImpulse));
    profiler.AddCounter("Physics.Fluid.RigidBody.TotalDisplacedFluidMass", static_cast<double>(m_LastStatistics.TotalDisplacedFluidMass));
}

bool FluidRigidBodyCoupling::ResolveParticleAgainstRigidBody(
    Scene& scene,
    PhysicsWorld& physicsWorld,
    Entity entity,
    FluidParticle& particle,
    const TransformComponent& transform,
    RigidBodyComponent& rigidBody,
    const ColliderComponent& collider,
    float deltaTime)
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

    // Position correction前のPenetrationDepthをBuoyancyの部分排除率へ利用します。
    // 補正後はParticle中心がCollider外へ移動するため、この値を後から再構築しないことが重要です。
    const float displacedFluidFraction = ComputeDisplacedFluidFraction(
        contact.PenetrationDepth,
        m_Settings.ParticleRadius);

    // Position correctionはParticle側だけへ適用し、RigidBody Transformは既存Contact Solverへ任せます。
    particle.Position = contact.CorrectedParticlePosition;

    const float particleMass = std::max(0.0f, particle.Mass);
    if (particleMass <= math::Epsilon)
    {
        return true;
    }

    const float particleInverseMass = 1.0f / particleMass;
    const math::Vec3 leverArm = contact.Point - transform.Position;
    const math::Mat3 bodyInverseInertia = ComputeWorldInverseInertia(
        &transform,
        &rigidBody,
        &collider);

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
    // SPH pressure reaction
    // ------------------------------------------------------------------------
    if (m_Settings.PressureReactionCoefficient > 0.0f && deltaTime > 0.0f)
    {
        // FluidParticleを半径rの代表面要素として扱い、投影面積 pi*r^2 へ正圧を作用させます。
        // SPHの線形EOSは自由表面付近で負圧を持つ場合がありますが、初版では境界への吸着を
        // 発生させないため正圧だけをRigidBody反作用へ変換します。
        const float positivePressure = std::max(0.0f, particle.Pressure);
        const float projectedArea = Pi * m_Settings.ParticleRadius * m_Settings.ParticleRadius;
        const float pressureImpulseMagnitude = positivePressure
            * projectedArea
            * m_Settings.PressureReactionCoefficient
            * deltaTime;

        if (pressureImpulseMagnitude > math::Epsilon)
        {
            const math::Vec3 pressureImpulseOnParticle = contact.Normal * pressureImpulseMagnitude;
            particle.Velocity += pressureImpulseOnParticle * particleInverseMass;
            physicsWorld.AddImpulseAtPoint(
                scene,
                entity,
                -pressureImpulseOnParticle,
                contact.Point);

            ++m_LastStatistics.AppliedPressureImpulseCount;
            m_LastStatistics.TotalPressureImpulse += pressureImpulseMagnitude;
        }
    }

    // ------------------------------------------------------------------------
    // Buoyancy impulse
    // ------------------------------------------------------------------------
    if (m_Settings.BuoyancyCoefficient > 0.0f
        && deltaTime > 0.0f
        && displacedFluidFraction > 0.0f)
    {
        // Particle MassをそのParticleが代表するFluid質量とみなし、Colliderが排除した割合だけを
        // displaced fluid massとして数えます。Archimedesの原理 F_b = -m_displaced * g を
        // impulseへ積分するため、流体密度を別の定数として二重管理する必要がありません。
        const float displacedFluidMass = particleMass
            * displacedFluidFraction
            * m_Settings.BuoyancyCoefficient;
        const math::Vec3 buoyancyImpulseOnBody = -physicsWorld.GetGravity()
            * displacedFluidMass
            * deltaTime;
        const float buoyancyImpulseMagnitude = buoyancyImpulseOnBody.Length();

        if (buoyancyImpulseMagnitude > math::Epsilon)
        {
            // BuoyancyはFluid内部の圧力勾配がBodyへ伝える反作用なので、Bodyだけへ外力を追加せず、
            // Particleへ等量反対向きImpulseを返してCoupling内部の運動量交換として扱います。
            physicsWorld.AddImpulseAtPoint(
                scene,
                entity,
                buoyancyImpulseOnBody,
                contact.Point);
            particle.Velocity -= buoyancyImpulseOnBody * particleInverseMass;

            ++m_LastStatistics.AppliedBuoyancyImpulseCount;
            m_LastStatistics.TotalBuoyancyImpulse += buoyancyImpulseMagnitude;
            m_LastStatistics.TotalDisplacedFluidMass += displacedFluidMass;
        }
    }

    // ------------------------------------------------------------------------
    // Tangential drag impulse
    // ------------------------------------------------------------------------
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
