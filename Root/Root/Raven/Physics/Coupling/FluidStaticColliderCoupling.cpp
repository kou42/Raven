#include "Raven/Physics/Coupling/FluidStaticColliderCoupling.h"

#include <algorithm>

#include "Raven/Core/CPUProfiler.h"
#include "Raven/Math/Math.h"
#include "Raven/Physics/Coupling/FluidColliderContact.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
constexpr float MinimumParticleRadius = 0.0f;
}

FluidStaticColliderCoupling::FluidStaticColliderCoupling(
    const FluidStaticColliderCouplingSettings& settings)
{
    SetSettings(settings);
}

void FluidStaticColliderCoupling::SetSettings(
    const FluidStaticColliderCouplingSettings& settings)
{
    m_Settings = settings;
    m_Settings.ParticleRadius = std::max(MinimumParticleRadius, settings.ParticleRadius);
    m_Settings.Restitution = std::clamp(settings.Restitution, 0.0f, 1.0f);
}

void FluidStaticColliderCoupling::ResolveScene(
    Scene& scene,
    std::vector<FluidParticle>& particles)
{
    RAVEN_PROFILE_SCOPE("Physics.Fluid.StaticColliderCoupling");
    m_LastStatistics = {};

    if (particles.empty())
    {
        return;
    }

    for (auto [entity, transform, collider] : scene.View<TransformComponent, ColliderComponent>())
    {
        if (collider.IsTrigger)
        {
            continue;
        }

        if (collider.Type != ColliderType::Sphere && collider.Type != ColliderType::Box)
        {
            continue;
        }

        const RigidBodyComponent* rigidBody =
            scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
        if (rigidBody != nullptr && rigidBody->Type != BodyType::Static)
        {
            // Dynamic / Kinematicは双方向Coupling側で扱います。
            continue;
        }

        ++m_LastStatistics.SupportedColliderCount;
        for (FluidParticle& particle : particles)
        {
            ++m_LastStatistics.CandidatePairCount;
            if (ResolveParticleAgainstCollider(particle, transform, collider))
            {
                ++m_LastStatistics.ResolvedContactCount;
            }
        }
    }

    CPUProfiler& profiler = CPUProfiler::Get();
    profiler.AddCounter(
        "Physics.Fluid.StaticCollider.SupportedColliderCount",
        static_cast<double>(m_LastStatistics.SupportedColliderCount));
    profiler.AddCounter(
        "Physics.Fluid.StaticCollider.CandidatePairCount",
        static_cast<double>(m_LastStatistics.CandidatePairCount));
    profiler.AddCounter(
        "Physics.Fluid.StaticCollider.ResolvedContactCount",
        static_cast<double>(m_LastStatistics.ResolvedContactCount));
}

bool FluidStaticColliderCoupling::ResolveParticleAgainstCollider(
    FluidParticle& particle,
    const TransformComponent& transform,
    const ColliderComponent& collider) const
{
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

    // Static側は動かないため、位置補正量をFluid Particleへ全量適用します。
    particle.Position = contact.CorrectedParticlePosition;
    ResolveVelocity(particle, contact.Normal, collider.Restitution);
    return true;
}

void FluidStaticColliderCoupling::ResolveVelocity(
    FluidParticle& particle,
    const math::Vec3& outwardNormal,
    float colliderRestitution) const
{
    const float normalVelocity = math::Vec3::Dot(particle.Velocity, outwardNormal);
    if (normalVelocity >= 0.0f)
    {
        return;
    }

    const float restitution = std::min(
        m_Settings.Restitution,
        std::clamp(colliderRestitution, 0.0f, 1.0f));
    particle.Velocity -= outwardNormal * ((1.0f + restitution) * normalVelocity);
}

} // namespace Raven::ph
