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

    // 接触しているFluidとRigidBody表面の相対速度を減衰させる係数です。
    // 0で無効、1で1回のCoupling解決時に可能な範囲まで相対接線速度を揃えます。
    float DragCoefficient = 0.0f;
};

struct FluidRigidBodyCouplingStatistics
{
    uint64_t DynamicBodyCount = 0u;
    uint64_t CandidatePairCount = 0u;
    uint64_t ResolvedContactCount = 0u;
    uint64_t AppliedImpulseCount = 0u;
    uint64_t AppliedDragImpulseCount = 0u;
    float TotalNormalImpulse = 0.0f;
    float TotalDragImpulse = 0.0f;
};

// ============================================================================
// Fluid <-> Dynamic RigidBody Coupling
// ============================================================================
// ParticleとDynamic RigidBody間で法線Impulseと接線Drag Impulseを双方向へ適用します。
// 接触幾何はStatic Couplingと共通化し、SPHのDensity / Pressure計算から独立させます。
//
// Normal / Drag双方でRigidBodyのworld-space逆慣性を有効質量へ含めます。
// これにより接触点が重心から外れた場合も、並進と回転へ使われるImpulse量を
// 同じ剛体力学モデルで計算できます。
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
