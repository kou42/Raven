#pragma once

#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Physics/Fluid/FluidParticle.h"
#include "Raven/Physics/Fluid/SPHSolver.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{

class Application;
class Material;
class Mesh;

// ============================================================================
// Fluid SPH Demo Layer
// ============================================================================
// SPHSolverのParticleを通常のSphere Entityへ同期し、Game View / Scene View上で
// Density -> Pressure -> Force -> Integration -> Boundary の結果を目視確認します。
// Simulation本体はSPHSolverへ閉じ、Layerは初期配置・描画Entity同期だけを担当します。
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

private:
    Application& m_Application;
    ph::SPHSolver m_Solver{};
    std::vector<ph::FluidParticle> m_Particles;
    std::vector<Entity> m_ParticleEntities;
    Ref<Mesh> m_ParticleMesh;
    Ref<Material> m_ParticleMaterial;
};

} // namespace Raven
