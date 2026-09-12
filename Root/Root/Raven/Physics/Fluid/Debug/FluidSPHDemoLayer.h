#pragma once

#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Physics/Coupling/FluidRigidBodyCoupling.h"
#include "Raven/Physics/Coupling/FluidStaticColliderCoupling.h"
#include "Raven/Physics/Fluid/FluidParticle.h"
#include "Raven/Physics/Fluid/SPHSolver.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{

class Application;
class Material;
class Mesh;
class Pipeline;

// ============================================================================
// Fluid SPH Demo Layer
// ============================================================================
// SPHSolverのParticleを通常のSphere Entityへ同期し、Game View / Scene View上で
// Density -> Pressure -> Force -> Integration -> Collider Coupling の結果を目視確認します。
// Simulation本体はSPHSolverへ閉じ、Scene Colliderとの接続はCoupling層へ分離します。
class FluidSPHDemoLayer final : public Layer
{
public:
    explicit FluidSPHDemoLayer(Application& application)
        : m_Application(application)
    {
    }

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float deltaTime) override;
    void OnRender() override;

private:
    void CreateParticles();
    void CreateRenderEntities();
    void SynchronizeRenderEntities();

    math::Vec3 ComputeParticleDebugColor(const ph::FluidParticle& particle) const;

private:
    Application& m_Application;
    ph::SPHSolver m_Solver{};
    ph::FluidStaticColliderCoupling m_StaticColliderCoupling{};
    ph::FluidRigidBodyCoupling m_RigidBodyCoupling{};
    std::vector<ph::FluidParticle> m_Particles;
    std::vector<Entity> m_ParticleEntities;
    Ref<Mesh> m_ParticleMesh;

    Ref<Pipeline> m_ParticlePipeline;
    std::vector<Ref<Material>> m_ParticleMaterials;
};

} // namespace Raven
