#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Physics/Fluid/FluidParticle.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{
class Scene;

namespace ph
{
class PhysicsWorld;

struct FluidRigidBodyCouplingSettings
{
    float ParticleRadius = 0.02f;
    float Restitution = 0.0f;
};

struct FluidRigidBodyCouplingStatistics
{
    uint64_t DynamicBodyCount = 0u;
    uint64_t CandidatePairCount = 0u;
    uint64_t ResolvedContactCount = 0u;
    uint64_t AppliedImpulseCount = 0u;
    float TotalNormalImpulse = 0.0f;
};

// ============================================================================
// Fluid <-> Dynamic RigidBody Coupling
// ============================================================================
// ParticleとDynamic RigidBody間で法線Impulseを双方向へ適用します。
// 接触幾何はStatic Couplingと共通化し、SPHのDensity / Pressure計算から独立させます。
//
// 現段階のImpulse有効質量はParticleとRigidBodyの並進InverseMassを使用します。
// 作用点Impulse自体はPhysicsWorld::AddImpulseAtPointへ渡すため角速度も更新されますが、
// r x I^-1 x r を含む回転有効質量はDrag / Pressure Reaction工程で拡張予定です。
class FluidRigidBodyCoupling
{
public:
    explicit FluidRigidBodyCoupling(
        const FluidRigidBodyCouplingSettings& settings = FluidRigidBodyCouplingSettings{});

    void SetSettings(const FluidRigidBodyCouplingSettings& settings);
    const FluidRigidBodyCouplingSettings& GetSettings() const { return m_Settings; }

    void ResolveScene(
        Scene& scene,
        PhysicsWorld& physicsWorld,
        std::vector<FluidParticle>& particles);

    // Self Testや将来Broad Phase候補から直接呼べる単一Body版です。
    bool ResolveParticleAgainstRigidBody(
        Scene& scene,
        PhysicsWorld& physicsWorld,
        Entity entity,
        FluidParticle& particle,
        const TransformComponent& transform,
        RigidBodyComponent& rigidBody,
        const ColliderComponent& collider);

    const FluidRigidBodyCouplingStatistics& GetLastStatistics() const
    {
        return m_LastStatistics;
    }

private:
    FluidRigidBodyCouplingSettings m_Settings{};
    FluidRigidBodyCouplingStatistics m_LastStatistics{};
};

} // namespace ph
} // namespace Raven
