#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Physics/Fluid/FluidParticle.h"
#include "Raven/Scene/Components.h"

namespace Raven
{
class Scene;

namespace ph
{

struct FluidStaticColliderCouplingSettings
{
    float ParticleRadius = 0.02f;
    float Restitution = 0.0f;
};

struct FluidStaticColliderCouplingStatistics
{
    uint64_t SupportedColliderCount = 0u;
    uint64_t CandidatePairCount = 0u;
    uint64_t ResolvedContactCount = 0u;
};

// ============================================================================
// FluidStaticColliderCoupling
// ============================================================================
// Static Colliderとの応答だけを担当します。接触幾何はFluidColliderContactへ分離し、
// Dynamic RigidBody Couplingと同じSphere / Box判定を共有します。
class FluidStaticColliderCoupling
{
public:
    explicit FluidStaticColliderCoupling(
        const FluidStaticColliderCouplingSettings& settings = FluidStaticColliderCouplingSettings{});

    void SetSettings(const FluidStaticColliderCouplingSettings& settings);
    const FluidStaticColliderCouplingSettings& GetSettings() const { return m_Settings; }

    // RigidBodyを持たないCollider、またはStatic RigidBodyだけを処理します。
    void ResolveScene(Scene& scene, std::vector<FluidParticle>& particles);

    bool ResolveParticleAgainstCollider(
        FluidParticle& particle,
        const TransformComponent& transform,
        const ColliderComponent& collider) const;

    const FluidStaticColliderCouplingStatistics& GetLastStatistics() const
    {
        return m_LastStatistics;
    }

private:
    void ResolveVelocity(FluidParticle& particle, const math::Vec3& outwardNormal,
        float colliderRestitution) const;

private:
    FluidStaticColliderCouplingSettings m_Settings{};
    FluidStaticColliderCouplingStatistics m_LastStatistics{};
};

} // namespace ph
} // namespace Raven
