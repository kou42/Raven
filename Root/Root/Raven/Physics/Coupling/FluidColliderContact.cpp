#include "Raven/Physics/Coupling/FluidColliderContact.h"

#include <algorithm>
#include <cmath>

#include "Raven/Math/Math.h"
#include "Raven/Physics/Collision/OBB.h"

namespace Raven::ph
{
namespace
{
math::Vec3 ComputeFallbackNormal(const FluidParticle& particle)
{
    if (particle.Velocity.LengthSq() > math::Epsilon * math::Epsilon)
    {
        // 中心が完全一致する退化ケースでは進行方向と反対へ押し出し、
        // Collider内部をさらに横切る方向へ補正しないようにします。
        return -particle.Velocity.Normalized();
    }
    return math::Vec3{ 0.0f, 1.0f, 0.0f };
}

bool GenerateSphereContact(
    const FluidParticle& particle,
    float particleRadius,
    const TransformComponent& transform,
    const ColliderComponent& collider,
    FluidColliderContact& outContact)
{
    const float colliderRadius = std::max(0.0f, collider.Radius);
    const float minimumDistance = colliderRadius + particleRadius;
    if (minimumDistance <= math::Epsilon)
    {
        return false;
    }

    // Sphere Offsetは既存RigidBody側の契約に合わせworld軸方向へ直接加算します。
    const math::Vec3 center = transform.Position + collider.Offset;
    const math::Vec3 displacement = particle.Position - center;
    const float distanceSq = displacement.LengthSq();
    if (distanceSq >= minimumDistance * minimumDistance)
    {
        return false;
    }

    float distance = 0.0f;
    math::Vec3 normal = ComputeFallbackNormal(particle);
    if (distanceSq > math::Epsilon * math::Epsilon)
    {
        distance = std::sqrt(distanceSq);
        normal = displacement / distance;
    }

    outContact.Normal = normal;
    outContact.Point = center + normal * colliderRadius;
    outContact.CorrectedParticlePosition = center + normal * minimumDistance;
    outContact.PenetrationDepth = minimumDistance - distance;
    return true;
}

bool GenerateBoxContact(
    const FluidParticle& particle,
    float particleRadius,
    const TransformComponent& transform,
    const ColliderComponent& collider,
    FluidColliderContact& outContact)
{
    OBB box{};
    if (ComputeBoxOBB(transform, collider, box) == false)
    {
        return false;
    }

    const math::Vec3 localPosition = box.ToLocalPoint(particle.Position);
    math::Vec3 closestPoint{
        std::clamp(localPosition.x, -box.HalfExtents.x, box.HalfExtents.x),
        std::clamp(localPosition.y, -box.HalfExtents.y, box.HalfExtents.y),
        std::clamp(localPosition.z, -box.HalfExtents.z, box.HalfExtents.z) };

    const math::Vec3 localDisplacement = localPosition - closestPoint;
    const float distanceSq = localDisplacement.LengthSq();
    if (distanceSq > particleRadius * particleRadius)
    {
        return false;
    }

    math::Vec3 localNormal{};
    math::Vec3 correctedLocalPosition{};
    math::Vec3 contactLocalPoint = closestPoint;
    float penetrationDepth = 0.0f;

    if (distanceSq > math::Epsilon * math::Epsilon)
    {
        // Box外側から面・辺・角へ接触する通常ケースです。
        const float distance = std::sqrt(distanceSq);
        localNormal = localDisplacement / distance;
        correctedLocalPosition = closestPoint + localNormal * particleRadius;
        penetrationDepth = particleRadius - distance;
    }
    else
    {
        // Particle中心がBox内部または表面上です。最も近い面へ最短で押し出します。
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
        contactLocalPoint = localPosition;
        contactLocalPoint[pushAxis] = sign * box.HalfExtents[pushAxis];
        correctedLocalPosition = contactLocalPoint + localNormal * particleRadius;
        penetrationDepth = distanceToFace[pushAxis] + particleRadius;
    }

    outContact.Normal = box.Axis[0] * localNormal.x
        + box.Axis[1] * localNormal.y
        + box.Axis[2] * localNormal.z;
    outContact.Point = box.ToWorldPoint(contactLocalPoint);
    outContact.CorrectedParticlePosition = box.ToWorldPoint(correctedLocalPosition);
    outContact.PenetrationDepth = penetrationDepth;
    return true;
}
}

bool GenerateFluidParticleColliderContact(
    const FluidParticle& particle,
    float particleRadius,
    const TransformComponent& transform,
    const ColliderComponent& collider,
    FluidColliderContact& outContact)
{
    const float radius = std::max(0.0f, particleRadius);
    if (collider.IsTrigger)
    {
        return false;
    }

    switch (collider.Type)
    {
    case ColliderType::Sphere:
        return GenerateSphereContact(particle, radius, transform, collider, outContact);
    case ColliderType::Box:
        return GenerateBoxContact(particle, radius, transform, collider, outContact);
    default:
        return false;
    }
}

} // namespace Raven::ph
