#pragma once

#include <vector>

#include "Raven/Physics/Fluid/FluidCouplingBinding.h"
#include "Raven/Physics/PhysicsWorld.h"

namespace Raven
{
class Scene;

namespace ph
{

// ============================================================================
// FluidStaticColliderCouplingHandle
// ============================================================================
// 既存Demoコードとの互換性を保つ設定Handleです。
// Coupling実行本体はFluidWorldのFixed Stepへ移行したため、ResolveScene()は実行を行いません。
class FluidStaticColliderCouplingHandle
{
public:
    FluidStaticColliderCouplingHandle() = default;

    explicit FluidStaticColliderCouplingHandle(FluidCouplingBinding& binding)
        : m_Binding(&binding)
    {
    }

    void Bind(FluidCouplingBinding& binding)
    {
        m_Binding = &binding;
        m_Binding->StaticColliderSettings = m_Settings;
    }

    void SetSettings(const FluidStaticColliderCouplingSettings& settings)
    {
        m_Settings = settings;
        if (m_Binding != nullptr)
        {
            m_Binding->StaticColliderSettings = settings;
        }
    }

    const FluidStaticColliderCouplingSettings& GetSettings() const
    {
        return m_Settings;
    }

    void ResolveScene(Scene& scene, std::vector<FluidParticle>& particles) const
    {
        // CouplingはFluidWorld::StepSimulation(Scene&, PhysicsWorld&, ...)がParticipant Simulation後に
        // 一括解決します。既存Demoの呼び出し形を一時的に維持するため、この入口はno-opです。
        (void)scene;
        (void)particles;
    }

private:
    FluidCouplingBinding* m_Binding = nullptr;
    FluidStaticColliderCouplingSettings m_Settings{};
};

// ============================================================================
// FluidRigidBodyCouplingHandle
// ============================================================================
class FluidRigidBodyCouplingHandle
{
public:
    FluidRigidBodyCouplingHandle() = default;

    explicit FluidRigidBodyCouplingHandle(FluidCouplingBinding& binding)
        : m_Binding(&binding)
    {
    }

    void Bind(FluidCouplingBinding& binding)
    {
        m_Binding = &binding;
        m_Binding->RigidBodySettings = m_Settings;
    }

    void SetSettings(const FluidRigidBodyCouplingSettings& settings)
    {
        m_Settings = settings;
        if (m_Binding != nullptr)
        {
            m_Binding->RigidBodySettings = settings;
        }
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
        // 実CouplingはFluidWorldのFixed Stepで一度だけ行います。
        (void)scene;
        (void)physicsWorld;
        (void)particles;
        (void)fixedDeltaTime;
    }

private:
    FluidCouplingBinding* m_Binding = nullptr;
    FluidRigidBodyCouplingSettings m_Settings{};
};

} // namespace ph
} // namespace Raven
