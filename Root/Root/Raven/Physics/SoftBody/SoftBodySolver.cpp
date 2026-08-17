#include "Raven/Physics/SoftBody/SoftBodySolver.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace Raven
{
namespace ph
{

uint32_t SoftBodySolver::AddParticle(const math::Vec3& position, float inverseMass)
{
    SoftBodyParticle particle{};
    particle.Position = position;
    particle.PreviousPosition = position;
    particle.Velocity = math::Vec3{};
    particle.InverseMass = std::max(0.0f, inverseMass);

    m_Particles.push_back(particle);
    return static_cast<uint32_t>(m_Particles.size() - 1u);
}

uint32_t SoftBodySolver::AddDistanceConstraint(uint32_t particleA, uint32_t particleB, float compliance)
{
    assert(particleA < m_Particles.size());
    assert(particleB < m_Particles.size());
    assert(particleA != particleB);

    XPBDDistanceConstraint constraint{};
    constraint.ParticleA = particleA;
    constraint.ParticleB = particleB;
    constraint.RestLength = (m_Particles[particleB].Position - m_Particles[particleA].Position).Length();
    constraint.Compliance = std::max(0.0f, compliance);
    constraint.Lambda = 0.0f;

    m_DistanceConstraints.push_back(constraint);
    return static_cast<uint32_t>(m_DistanceConstraints.size() - 1u);
}

uint32_t SoftBodySolver::AddSphereCollider(const math::Vec3& center, float radius)
{
    SoftBodySphereCollider collider{};
    collider.Center = center;
    collider.Radius = std::max(0.0f, radius);

    m_SphereColliders.push_back(collider);
    return static_cast<uint32_t>(m_SphereColliders.size() - 1u);
}

void SoftBodySolver::SetSphereCollider(uint32_t colliderIndex, const math::Vec3& center, float radius)
{
    if (colliderIndex >= m_SphereColliders.size())
    {
        return;
    }

    SoftBodySphereCollider& collider = m_SphereColliders[colliderIndex];
    collider.Center = center;
    collider.Radius = std::max(0.0f, radius);
}

void SoftBodySolver::ClearSphereColliders()
{
    m_SphereColliders.clear();
}

uint32_t SoftBodySolver::AddPlaneCollider(const math::Vec3& normal, float offset)
{
    SoftBodyPlaneCollider collider{};
    collider.Normal = normal;
    collider.Offset = offset;

    if (collider.Normal.LengthSq() <= math::Epsilon * math::Epsilon)
    {
        collider.Normal = { 0.0f, 1.0f, 0.0f };
    }
    else
    {
        collider.Normal.Normalize();
    }

    m_PlaneColliders.push_back(collider);
    return static_cast<uint32_t>(m_PlaneColliders.size() - 1u);
}

void SoftBodySolver::SetPlaneCollider(uint32_t colliderIndex, const math::Vec3& normal, float offset)
{
    if (colliderIndex >= m_PlaneColliders.size())
    {
        return;
    }

    SoftBodyPlaneCollider& collider = m_PlaneColliders[colliderIndex];
    collider.Normal = normal;
    collider.Offset = offset;

    if (collider.Normal.LengthSq() <= math::Epsilon * math::Epsilon)
    {
        collider.Normal = { 0.0f, 1.0f, 0.0f };
    }
    else
    {
        collider.Normal.Normalize();
    }
}

void SoftBodySolver::ClearPlaneColliders()
{
    m_PlaneColliders.clear();
}

void SoftBodySolver::Clear()
{
    m_Particles.clear();
    m_DistanceConstraints.clear();
    m_SphereColliders.clear();
    m_PlaneColliders.clear();
}

void SoftBodySolver::Step(float deltaTime)
{
    if (deltaTime <= 0.0f)
    {
        return;
    }

    PredictPositions(deltaTime);
    ResetConstraintLambdas();

    const uint32_t iterationCount = std::max(1u, m_Settings.SolverIterations);
    for (uint32_t iteration = 0u; iteration < iterationCount; ++iteration)
    {
        // 内部Constraintを先に解き、その結果Collider内部へ戻ったParticleを同じ反復内で
        // Sphere / Planeの外側へ押し出します。
        SolveDistanceConstraints(deltaTime);
        SolveSphereCollisions();
        SolvePlaneCollisions();
    }

    UpdateVelocities(deltaTime);
}

void SoftBodySolver::PredictPositions(float deltaTime)
{
    for (SoftBodyParticle& particle : m_Particles)
    {
        particle.PreviousPosition = particle.Position;

        if (particle.IsFixed())
        {
            particle.Velocity = math::Vec3{};
            continue;
        }

        particle.Velocity += m_Gravity * deltaTime;
        particle.Position += particle.Velocity * deltaTime;
    }
}

void SoftBodySolver::ResetConstraintLambdas()
{
    for (XPBDDistanceConstraint& constraint : m_DistanceConstraints)
    {
        constraint.Lambda = 0.0f;
    }
}

void SoftBodySolver::SolveDistanceConstraints(float deltaTime)
{
    const float deltaTimeSq = deltaTime * deltaTime;

    for (XPBDDistanceConstraint& constraint : m_DistanceConstraints)
    {
        assert(constraint.ParticleA < m_Particles.size());
        assert(constraint.ParticleB < m_Particles.size());

        SoftBodyParticle& particleA = m_Particles[constraint.ParticleA];
        SoftBodyParticle& particleB = m_Particles[constraint.ParticleB];

        const float inverseMassSum = particleA.InverseMass + particleB.InverseMass;
        if (inverseMassSum <= 0.0f)
        {
            continue;
        }

        const math::Vec3 delta = particleB.Position - particleA.Position;
        const float distance = delta.Length();
        if (distance <= math::Epsilon)
        {
            continue;
        }

        const math::Vec3 normal = delta / distance;
        const float constraintValue = distance - constraint.RestLength;
        const float alphaTilde = constraint.Compliance / deltaTimeSq;
        const float denominator = inverseMassSum + alphaTilde;

        if (denominator <= math::Epsilon)
        {
            continue;
        }

        const float deltaLambda =
            (-constraintValue - alphaTilde * constraint.Lambda) / denominator;
        constraint.Lambda += deltaLambda;

        if (particleA.IsFixed() == false)
        {
            particleA.Position -= normal * (particleA.InverseMass * deltaLambda);
        }

        if (particleB.IsFixed() == false)
        {
            particleB.Position += normal * (particleB.InverseMass * deltaLambda);
        }
    }
}

void SoftBodySolver::SolveSphereCollisions()
{
    if (m_SphereColliders.empty())
    {
        return;
    }

    const float collisionThickness = std::max(0.0f, m_Settings.CollisionThickness);

    for (SoftBodyParticle& particle : m_Particles)
    {
        if (particle.IsFixed())
        {
            continue;
        }

        for (const SoftBodySphereCollider& collider : m_SphereColliders)
        {
            const float targetRadius = collider.Radius + collisionThickness;
            if (targetRadius <= 0.0f)
            {
                continue;
            }

            const math::Vec3 centerToParticle = particle.Position - collider.Center;
            const float distanceSq = centerToParticle.LengthSq();
            const float targetRadiusSq = targetRadius * targetRadius;

            if (distanceSq >= targetRadiusSq)
            {
                continue;
            }

            math::Vec3 normal{ 0.0f, 1.0f, 0.0f };
            if (distanceSq > math::Epsilon * math::Epsilon)
            {
                normal = centerToParticle / std::sqrt(distanceSq);
            }

            particle.Position = collider.Center + normal * targetRadius;
        }
    }
}

void SoftBodySolver::SolvePlaneCollisions()
{
    if (m_PlaneColliders.empty())
    {
        return;
    }

    const float collisionThickness = std::max(0.0f, m_Settings.CollisionThickness);

    for (SoftBodyParticle& particle : m_Particles)
    {
        if (particle.IsFixed())
        {
            continue;
        }

        for (const SoftBodyPlaneCollider& collider : m_PlaneColliders)
        {
            // Plane外側条件:
            //   dot(n, x) - offset >= thickness
            //
            // 条件を満たさないParticleだけを法線方向へ最短距離で射影します。
            const float signedDistance =
                math::Vec3::Dot(collider.Normal, particle.Position) - collider.Offset;

            if (signedDistance >= collisionThickness)
            {
                continue;
            }

            const float correctionDistance = collisionThickness - signedDistance;
            particle.Position += collider.Normal * correctionDistance;
        }
    }
}

void SoftBodySolver::UpdateVelocities(float deltaTime)
{
    const float inverseDeltaTime = 1.0f / deltaTime;

    for (SoftBodyParticle& particle : m_Particles)
    {
        if (particle.IsFixed())
        {
            particle.Velocity = math::Vec3{};
            continue;
        }

        particle.Velocity = (particle.Position - particle.PreviousPosition) * inverseDeltaTime;
    }
}

} // namespace ph
} // namespace Raven
