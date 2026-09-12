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

// ============================================================================
// Fluid <-> Static Collider Coupling Settings
// ============================================================================
// Fluid SolverそのものへScene / RigidBody依存を入れないため、異なる物理Domain間の接続は
// Coupling層で扱います。将来Macroscopic/Fluid・RigidBodyへ再編した場合も、この境界を
// Domain間Adapterとして移動できるよう、SPH固有のDensity/Pressure計算には依存しません。
struct FluidStaticColliderCouplingSettings
{
    // FluidParticleを衝突判定上のSphereとして扱う半径です。
    float ParticleRadius = 0.02f;

    // Fluid側の反発係数です。Collider側Restitutionとの小さい方を採用し、
    // 既存RigidBody Contact Manifoldと同じ合成規約へ揃えます。
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
// 現段階ではSphere / Box Static Colliderを対象にParticleの位置・速度だけを補正します。
// Static側へImpulseを返さないことを明示した専用Couplingです。
//
// 次段階のFluid <-> Dynamic RigidBodyでは同じ幾何Contact生成を再利用し、反作用Impulseを
// RigidBodyへ返す層へ拡張します。そのためSPHSolverからPhysicsWorldを直接参照しません。
class FluidStaticColliderCoupling
{
public:
    explicit FluidStaticColliderCoupling(
        const FluidStaticColliderCouplingSettings& settings = FluidStaticColliderCouplingSettings{});

    void SetSettings(const FluidStaticColliderCouplingSettings& settings);
    const FluidStaticColliderCouplingSettings& GetSettings() const { return m_Settings; }

    // Scene内の「RigidBodyなしCollider」またはStatic RigidBody Colliderだけを処理します。
    // Dynamic / Kinematicは次工程の双方向Couplingへ残し、ここでは一方向反応させません。
    void ResolveScene(Scene& scene, std::vector<FluidParticle>& particles);

    // Unit/Self Testおよび将来のBroad Phase候補処理から再利用できる単一Collider版です。
    bool ResolveParticleAgainstCollider(
        FluidParticle& particle,
        const TransformComponent& transform,
        const ColliderComponent& collider) const;

    const FluidStaticColliderCouplingStatistics& GetLastStatistics() const
    {
        return m_LastStatistics;
    }

private:
    bool ResolveParticleAgainstSphere(
        FluidParticle& particle,
        const TransformComponent& transform,
        const ColliderComponent& collider) const;
    bool ResolveParticleAgainstBox(
        FluidParticle& particle,
        const TransformComponent& transform,
        const ColliderComponent& collider) const;
    void ResolveVelocity(FluidParticle& particle, const math::Vec3& outwardNormal,
        float colliderRestitution) const;

private:
    FluidStaticColliderCouplingSettings m_Settings{};
    FluidStaticColliderCouplingStatistics m_LastStatistics{};
};

} // namespace ph
} // namespace Raven
