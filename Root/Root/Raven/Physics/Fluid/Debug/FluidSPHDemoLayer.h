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
class Pipeline;

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

    // RestDensity付近を水色、低密度/負圧側を青、高密度/正圧側を赤へ写像します。
    // DensityとPressureを分けて参照することで、将来EOSを非線形化しても可視化側を拡張できます。
    math::Vec3 ComputeParticleDebugColor(const ph::FluidParticle& particle) const;

private:
    Application& m_Application;
    ph::SPHSolver m_Solver{};
    std::vector<ph::FluidParticle> m_Particles;
    std::vector<Entity> m_ParticleEntities;
    Ref<Mesh> m_ParticleMesh;

    // 全Particleは同じPipeline/Meshを共有し、Material instanceだけを分離します。
    // u_TintはMaterialに保存されるため、1つのMaterialを共有すると最後に設定したParticle色で
    // 全Entityが描画されてしまいます。Debug Demoでは288個程度なので、色の正しさを優先します。
    Ref<Pipeline> m_ParticlePipeline;
    std::vector<Ref<Material>> m_ParticleMaterials;
};

} // namespace Raven
