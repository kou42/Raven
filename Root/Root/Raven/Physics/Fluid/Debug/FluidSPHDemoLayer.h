#pragma once

#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Physics/Fluid/FluidCouplingBinding.h"
#include "Raven/Physics/Fluid/FluidParticle.h"
#include "Raven/Physics/Fluid/FluidSimulationParticipant.h"
#include "Raven/Physics/Fluid/FluidWorldCouplingHandle.h"
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
// Simulation本体はSPHSolverへ閉じ、Scene Collider / RigidBodyとの接続実装はFluidWorldへ集約します。
// Fixed Step実行はFluidSimulationParticipantとしてPhysicsSimulationWorldへ参加します。
class FluidSPHDemoLayer final : public Layer, public ph::FluidSimulationParticipant
{
public:
    explicit FluidSPHDemoLayer(Application& application)
        : m_Application(application)
        , m_StaticColliderCoupling(m_CouplingBinding)
        , m_RigidBodyCoupling(m_CouplingBinding)
    {
        // Particle vector object自体のaddressはLayer lifetime中不変です。
        // vector内部bufferの再配置とは独立して、Bindingはvector objectを非所有参照します。
        m_CouplingBinding.Particles = &m_Particles;
    }

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float deltaTime) override;
    void OnRender() override;

    // FluidSimulationParticipant
    void SimulateFluid(float fixedDeltaTime) override;
    ph::FluidCouplingBinding* GetFluidCouplingBinding() override
    {
        return &m_CouplingBinding;
    }
    void SynchronizeFluidOutput() override;

private:
    void CreateParticles();
    void CreateRenderEntities();
    void CreateDemoTank();
    void SynchronizeRenderEntities();

    // RestDensity付近を水色、低密度/負圧側を青、高密度/正圧側を赤へ写像します。
    // DensityとPressureを分けて参照することで、将来EOSを非線形化しても可視化側を拡張できます。
    math::Vec3 ComputeParticleDebugColor(const ph::FluidParticle& particle) const;

private:
    Application& m_Application;
    ph::SPHSolver m_Solver{};

    std::vector<ph::FluidParticle> m_Particles;

    // DemoはParticle群とCoupling設定だけを保持します。
    // Scene Collider走査やRigidBody反作用の実行責務はFluidWorldのFixed Stepへ集約します。
    ph::FluidCouplingBinding m_CouplingBinding{};

    // 既存Demo初期化コードのSetSettings()呼び出しをBindingへ転送する互換Handleです。
    // ResolveScene()はno-opで、Couplingの実処理はFluidWorldだけが行います。
    ph::FluidStaticColliderCouplingHandle m_StaticColliderCoupling;
    ph::FluidRigidBodyCouplingHandle m_RigidBodyCoupling;

    std::vector<Entity> m_ParticleEntities;
    // 水槽壁と落下テストBodyもLayerの寿命に合わせて明示的に破棄します。
    std::vector<Entity> m_DemoEntities;
    Ref<Mesh> m_ParticleMesh;
    Ref<Mesh> m_DemoCubeMesh;

    // 全Particleは同じPipeline/Meshを共有し、Material instanceだけを分離します。
    // u_TintはMaterialに保存されるため、1つのMaterialを共有すると最後に設定したParticle色で
    // 全Entityが描画されてしまいます。Debug Demoでは288個程度なので、色の正しさを優先します。
    Ref<Pipeline> m_ParticlePipeline;
    std::vector<Ref<Material>> m_ParticleMaterials;
};

} // namespace Raven
