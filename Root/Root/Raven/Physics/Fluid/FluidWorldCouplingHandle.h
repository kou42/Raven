#pragma once

#include <vector>

#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{

// ============================================================================
// FluidStaticColliderCouplingHandle
// ============================================================================
// Fluid Participant側が保持する軽量な設定Handleです。
// 実際のFluidStaticColliderCouplingはFluidWorldが所有し、このHandleはResolve時に
// Participant固有設定とParticle配列だけをFluidWorldへ渡します。
class FluidStaticColliderCouplingHandle
{
public:
    void SetSettings(const FluidStaticColliderCouplingSettings& settings)
    {
        m_Settings = settings;
    }

    const FluidStaticColliderCouplingSettings& GetSettings() const
    {
        return m_Settings;
    }

    void ResolveScene(Scene& scene, std::vector<FluidParticle>& particles) const
    {
        FluidWorld& fluidWorld = scene.GetPhysicsSimulationWorld().GetFluidWorld();
        fluidWorld.SetStaticColliderCouplingSettings(m_Settings);
        fluidWorld.ResolveStaticColliderCoupling(scene, particles);
    }

private:
    FluidStaticColliderCouplingSettings m_Settings{};
};

// ============================================================================
// FluidRigidBodyCouplingHandle
// ============================================================================
// Dynamic RigidBody CouplingについてもSolver本体はFluidWorldへ集約し、Participant側には
// Simulation固有の設定値だけを残します。PhysicsWorld参照は既存Coupling APIとの互換性を維持します。
class FluidRigidBodyCouplingHandle
{
public:
    void SetSettings(const FluidRigidBodyCouplingSettings& settings)
    {
        m_Settings = settings;
    }

    const FluidRigidBodyCouplingSettings& GetSettings() const
    {
        return m_Settings;
    }

    void ResolveScene(
        Scene& scene,
        PhysicsWorld& physicsWorld,
        std::vector<FluidParticle>& particles,
        float fixedDeltaTime = 0.0f) const
    {
        FluidWorld& fluidWorld = scene.GetPhysicsSimulationWorld().GetFluidWorld();
        fluidWorld.SetRigidBodyCouplingSettings(m_Settings);
        fluidWorld.ResolveRigidBodyCoupling(
            scene,
            physicsWorld,
            particles,
            fixedDeltaTime);
    }

private:
    FluidRigidBodyCouplingSettings m_Settings{};
};

} // namespace Raven::ph
