#include "Raven/Physics/Coupling/FluidStaticColliderCoupling.h"

#include <algorithm>
#include <cmath>

#include "Raven/Core/CPUProfiler.h"
#include "Raven/Math/Math.h"
#include "Raven/Physics/Collision/OBB.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
constexpr float MinimumParticleRadius = 0.0f;

math::Vec3 ComputeFallbackNormal(const FluidParticle& particle)
{
    if (particle.Velocity.LengthSq() > math::Epsilon * math::Epsilon)
    {
        // 完全に中心が一致した場合は、現在速度と反対側へ押し出すと
        // 進行方向へさらにCollider内部を横切ることを避けられます。
        return -particle.Velocity.Normalized();
    }
    return math::Vec3{ 0.0f, 1.0f, 0.0f };
}
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
            // Dynamic / Kinematicへ一方向だけ位置反応させると運動量保存が崩れます。
            // それらは次段階のFluid <-> RigidBody双方向Couplingで処理します。
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
    if (collider.IsTrigger)
    {
        return false;
    }

    switch (collider.Type)
    {
    case ColliderType::Sphere:
        return ResolveParticleAgainstSphere(particle, transform, collider);
    case ColliderType::Box:
        return ResolveParticleAgainstBox(particle, transform, collider);
    default:
        return false;
    }
}

bool FluidStaticColliderCoupling::ResolveParticleAgainstSphere(
    FluidParticle& particle,
    const TransformComponent& transform,
    const ColliderComponent& collider) const
{
    const float colliderRadius = std::max(0.0f, collider.Radius);
    const float minimumDistance = colliderRadius + m_Settings.ParticleRadius;
    if (minimumDistance <= math::Epsilon)
    {
        return false;
    }

    // Sphere ColliderのOffsetは既存PhysicsWorld契約に合わせてworld軸方向へ直接加算します。
    const math::Vec3 colliderCenter = transform.Position + collider.Offset;
    const math::Vec3 displacement = particle.Position - colliderCenter;
    const float distanceSq = displacement.LengthSq();
    const float minimumDistanceSq = minimumDistance * minimumDistance;
    if (distanceSq >= minimumDistanceSq)
    {
        return false;
    }

    math::Vec3 outwardNormal = ComputeFallbackNormal(particle);
    if (distanceSq > math::Epsilon * math::Epsilon)
    {
        outwardNormal = displacement / std::sqrt(distanceSq);
    }

    // Particle中心をCollider表面 + Particle Radiusまで直接押し出します。
    // Static側は動かないため、位置補正量をFluid Particleだけへ全量適用します。
    particle.Position = colliderCenter + outwardNormal * minimumDistance;
    ResolveVelocity(particle, outwardNormal, collider.Restitution);
    return true;
}

bool FluidStaticColliderCoupling::ResolveParticleAgainstBox(
    FluidParticle& particle,
    const TransformComponent& transform,
    const ColliderComponent& collider) const
{
    OBB box{};
    if (ComputeBoxOBB(transform, collider, box) == false)
    {
        return false;
    }

    const float radius = m_Settings.ParticleRadius;
    const math::Vec3 localPosition = box.ToLocalPoint(particle.Position);
    math::Vec3 closestPoint{
        std::clamp(localPosition.x, -box.HalfExtents.x, box.HalfExtents.x),
        std::clamp(localPosition.y, -box.HalfExtents.y, box.HalfExtents.y),
        std::clamp(localPosition.z, -box.HalfExtents.z, box.HalfExtents.z) };

    const math::Vec3 localDisplacement = localPosition - closestPoint;
    const float distanceSq = localDisplacement.LengthSq();
    if (distanceSq > radius * radius)
    {
        return false;
    }

    math::Vec3 localNormal{};
    math::Vec3 correctedLocalPosition{};

    if (distanceSq > math::Epsilon * math::Epsilon)
    {
        // Box外側から角・辺・面へ接触した通常ケースです。
        const float distance = std::sqrt(distanceSq);
        localNormal = localDisplacement / distance;
        correctedLocalPosition = closestPoint + localNormal * radius;
    }
    else
    {
        // Particle中心がBox内部、または表面と一致しています。
        // 最も近い面へ最短距離で押し出し、Particle Radius分だけ外側へ離します。
        const float distanceToFace[3] = {
            box.HalfExtents.x - std::abs(localPosition.x),
            box.HalfExtents.y - std::abs(localPosition.y),
            box.HalfExtents.z - std::abs(localPosition.z) };

        int pushAxis = 0;
        if (distanceToFace[1] < distanceToFace[pushAxis])
        {
            pushAxis = 1;
        }
        if (distanceToFace[2] < distanceToFace[pushAxis])
        {
            pushAxis = 2;
        }

        const float sign = localPosition[pushAxis] >= 0.0f ? 1.0f : -1.0f;
        localNormal[pushAxis] = sign;
        correctedLocalPosition = localPosition;
        correctedLocalPosition[pushAxis] = sign * (box.HalfExtents[pushAxis] + radius);
    }

    const math::Vec3 outwardNormal =
        box.Axis[0] * localNormal.x
        + box.Axis[1] * localNormal.y
        + box.Axis[2] * localNormal.z;

    particle.Position = box.ToWorldPoint(correctedLocalPosition);
    ResolveVelocity(particle, outwardNormal, collider.Restitution);
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
        // 既にCollider外側へ離れている速度成分は維持します。
        return;
    }

    const float restitution = std::min(
        m_Settings.Restitution,
        std::clamp(colliderRestitution, 0.0f, 1.0f));
    particle.Velocity -= outwardNormal * ((1.0f + restitution) * normalVelocity);
}

} // namespace Raven::ph
