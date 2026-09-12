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

    // SPH Particleが保持する正圧をCollider表面への面圧としてRigidBodyへ返す倍率です。
    // 0で無効です。初版では負圧を吸着へ変換せず0へクランプします。
    float PressureReactionCoefficient = 0.0f;

    // Colliderと重なったFluid Particleの排除質量からArchimedes相当の浮力を構築する倍率です。
    // 0で無効、1を基準値とします。局所排除量はParticle半径とPenetrationDepthから近似します。
    float BuoyancyCoefficient = 0.0f;
};

struct FluidRigidBodyCouplingStatistics
{
    uint64_t DynamicBodyCount = 0u;
    uint64_t CandidatePairCount = 0u;
    uint64_t ResolvedContactCount = 0u;
    uint64_t AppliedImpulseCount = 0u;
    uint64_t AppliedDragImpulseCount = 0u;
    uint64_t AppliedPressureImpulseCount = 0u;
    uint64_t AppliedBuoyancyImpulseCount = 0u;
    float TotalNormalImpulse = 0.0f;
    float TotalDragImpulse = 0.0f;
    float TotalPressureImpulse = 0.0f;
    float TotalBuoyancyImpulse = 0.0f;
    float TotalDisplacedFluidMass = 0.0f;
};

// ============================================================================
// Fluid <-> Dynamic RigidBody Coupling
// ============================================================================
// ParticleとDynamic RigidBody間で法線衝突、接線Drag、SPH Pressure Reaction、
// 排除Fluid質量に基づくBuoyancyを双方向へ適用します。
// 接触幾何はStatic Couplingと共通化し、SPH Solver自体へScene / RigidBody依存を入れません。
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
        std::vector<FluidParticle>& particles,
        float deltaTime = 0.0f);

    // Self Testや将来Broad Phase候補から直接呼べる単一Body版です。
    bool ResolveParticleAgainstRigidBody(
        Scene& scene,
        PhysicsWorld& physicsWorld,
        Entity entity,
        FluidParticle& particle,
        const TransformComponent& transform,
        RigidBodyComponent& rigidBody,
        const ColliderComponent& collider,
        float deltaTime = 0.0f);

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
