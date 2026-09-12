#include "Raven/Physics/Fluid/Debug/FluidSPHDemoLayer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "Raven/Core/Application.h"
#include "Raven/Math/Math.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{
namespace
{
constexpr uint32_t ParticleCountX = 6u;
constexpr uint32_t ParticleCountY = 8u;
constexpr uint32_t ParticleCountZ = 6u;
constexpr float ParticleSpacing = 0.32f;
constexpr float SmoothingRadius = 0.55f;
constexpr float ParticleMass = 1.0f;
constexpr float RenderParticleRadius = 0.12f;
constexpr float DensityVisualizationRange = 0.15f;
constexpr math::Vec3 FluidOrigin{ -0.8f, 4.0f, -0.8f };
constexpr math::Vec3 BoundaryMinimum{ -4.0f, 0.2f, -4.0f };
constexpr math::Vec3 BoundaryMaximum{ 4.0f, 8.0f, 4.0f };
constexpr math::Vec3 LowDensityColor{ 0.08f, 0.25f, 1.00f };
constexpr math::Vec3 RestDensityColor{ 0.10f, 0.80f, 0.95f };
constexpr math::Vec3 HighDensityColor{ 1.00f, 0.20f, 0.08f };

math::Vec3 LerpColor(const math::Vec3& a, const math::Vec3& b, float t)
{
    const float clampedT = std::clamp(t, 0.0f, 1.0f);
    return a + (b - a) * clampedT;
}
}

void FluidSPHDemoLayer::OnAttach()
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr)
    {
        return;
    }

    CreateParticles();

    // 初期格子のDensityを一度測り、その平均値をEOSのRestDensityへ採用します。
    // Kernel・Mass・Spacingの組み合わせから自然に決まる密度を基準にすることで、
    // Demo固有の見た目調整値をSPHSolver本体へ埋め込まずに済みます。
    ph::SPHSettings initialSettings{};
    initialSettings.SmoothingRadius = SmoothingRadius;
    initialSettings.PressureStiffness = 0.0f;
    initialSettings.Viscosity = 0.35f;
    initialSettings.Gravity = { 0.0f, -9.81f, 0.0f };
    initialSettings.BoundaryEnabled = true;
    initialSettings.BoundaryMinimum = BoundaryMinimum;
    initialSettings.BoundaryMaximum = BoundaryMaximum;
    initialSettings.BoundaryParticleRadius = RenderParticleRadius;
    initialSettings.BoundaryRestitution = 0.05f;
    initialSettings.CFLFactor = 0.35f;
    initialSettings.AccelerationTimeStepFactor = 0.20f;
    initialSettings.MaximumSubsteps = 16u;
    m_Solver.SetSettings(initialSettings);

    // Fluid粒子の見た目半径とCollider判定半径を揃えます。
    // Coupling設定はSPHSolverから独立させ、将来RigidBody / SoftBodyとの双方向Couplingへ
    // 発展させてもFluid Solverの設定構造へ他Domain固有値を混ぜないようにします。
    ph::FluidStaticColliderCouplingSettings couplingSettings{};
    couplingSettings.ParticleRadius = RenderParticleRadius;
    couplingSettings.Restitution = initialSettings.BoundaryRestitution;
    m_StaticColliderCoupling.SetSettings(couplingSettings);

    m_Solver.ComputeDensity(m_Particles);

    float densitySum = 0.0f;
    for (const ph::FluidParticle& particle : m_Particles)
    {
        densitySum += particle.Density;
    }

    ph::SPHSettings simulationSettings = initialSettings;
    if (m_Particles.empty() == false)
    {
        simulationSettings.RestDensity = densitySum / static_cast<float>(m_Particles.size());
    }
    simulationSettings.PressureStiffness = 35.0f;
    m_Solver.SetSettings(simulationSettings);
    m_Solver.ComputePressure(m_Particles);

    m_ParticleMesh = PrimitiveMeshFactory::CreateSphere(8u, 6u);
    if (m_ParticleMesh == nullptr)
    {
        return;
    }

    ShaderLibrary shaderLibrary{};
    Ref<Shader> shader = shaderLibrary.Load(
        "FluidSPHDemo",
        "Raven/Assets/Shaders/Vertex/test.vert",
        "Raven/Assets/Shaders/Fragment/test.frag");
    if (shader == nullptr)
    {
        m_ParticleMesh.reset();
        return;
    }

    PipelineSpecification pipelineSpecification{};
    pipelineSpecification.DebugName = "Fluid SPH Demo Pipeline";
    pipelineSpecification.Shader = shader;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.DepthCompare = DepthCompareOperator::Less;
    pipelineSpecification.Blend = true;

    // GPU Pipelineは全Particleで共有します。MaterialだけをParticleごとに分け、
    // u_Tintの状態が別Particleへ漏れないようにします。
    m_ParticlePipeline = Pipeline::Create(pipelineSpecification);
    if (m_ParticlePipeline == nullptr)
    {
        m_ParticleMesh.reset();
        return;
    }

    CreateRenderEntities();
    SynchronizeRenderEntities();
}

void FluidSPHDemoLayer::OnDetach()
{
    Scene* scene = m_Application.GetScene();
    if (scene != nullptr)
    {
        for (Entity& entity : m_ParticleEntities)
        {
            if (static_cast<bool>(entity) && scene->IsEntityAlive(entity))
            {
                scene->DestroyEntity(entity);
            }
        }
    }

    m_ParticleEntities.clear();
    m_ParticleMaterials.clear();
    m_Particles.clear();
    m_ParticlePipeline.reset();
    m_ParticleMesh.reset();
}

void FluidSPHDemoLayer::OnUpdate(float deltaTime)
{
    if (m_Particles.empty())
    {
        return;
    }

    // Application frameの極端なstallをそのままSPHへ渡さないよう上限を設けます。
    // その内側ではSPHSolver自身のStable Time Stepが必要なSubstepへ分割します。
    const float safeDeltaTime = std::clamp(deltaTime, 0.0f, 0.0333333f);
    if (safeDeltaTime <= 0.0f)
    {
        return;
    }

    m_Solver.Step(m_Particles, safeDeltaTime);

    Scene* scene = m_Application.GetScene();
    if (scene != nullptr)
    {
        // Static Colliderとの幾何接触はSPHSolverの外で解決します。
        // 現段階は一方向CouplingなのでStatic側へImpulseは返しません。
        m_StaticColliderCoupling.ResolveScene(*scene, m_Particles);
    }

    SynchronizeRenderEntities();
}

void FluidSPHDemoLayer::OnRender()
{
    // Particleは通常のMeshRendererComponentとしてSceneへ登録済みです。
    // SceneGame::RenderScene()のECS描画経路へ自動参加するため、専用Render処理は不要です。
}

void FluidSPHDemoLayer::CreateParticles()
{
    m_Particles.clear();
    m_Particles.reserve(
        static_cast<std::size_t>(ParticleCountX)
        * static_cast<std::size_t>(ParticleCountY)
        * static_cast<std::size_t>(ParticleCountZ));

    for (uint32_t y = 0u; y < ParticleCountY; ++y)
    {
        for (uint32_t z = 0u; z < ParticleCountZ; ++z)
        {
            for (uint32_t x = 0u; x < ParticleCountX; ++x)
            {
                ph::FluidParticle particle{};
                particle.Position = FluidOrigin + math::Vec3{
                    static_cast<float>(x) * ParticleSpacing,
                    static_cast<float>(y) * ParticleSpacing,
                    static_cast<float>(z) * ParticleSpacing };
                particle.Mass = ParticleMass;
                m_Particles.push_back(particle);
            }
        }
    }
}

void FluidSPHDemoLayer::CreateRenderEntities()
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr || m_ParticleMesh == nullptr || m_ParticlePipeline == nullptr)
    {
        return;
    }

    m_ParticleEntities.clear();
    m_ParticleMaterials.clear();
    m_ParticleEntities.reserve(m_Particles.size());
    m_ParticleMaterials.reserve(m_Particles.size());

    for (std::size_t i = 0u; i < m_Particles.size(); ++i)
    {
        // Material instanceはParticleごとに分けますが、内部Pipelineは共有Refです。
        // Debug Demo規模ではUniform状態の明確さを優先し、Renderer全体へ個別Tint機構を追加しません。
        Ref<Material> material = CreateRef<Material>(m_ParticlePipeline);
        material->SetUniform("u_Tint", ComputeParticleDebugColor(m_Particles[i]));
        material->SetUniform("u_Alpha", 1.0f);

        Entity entity = scene->CreateEntity("SPH Fluid Particle");
        entity.AddComponent<MeshRendererComponent>(
            MeshRendererComponent{ m_ParticleMesh, material });

        m_ParticleMaterials.push_back(std::move(material));
        m_ParticleEntities.push_back(entity);
    }
}

math::Vec3 FluidSPHDemoLayer::ComputeParticleDebugColor(
    const ph::FluidParticle& particle) const
{
    const ph::SPHSettings& settings = m_Solver.GetSettings();

    // Density偏差はRestDensityに対する割合で正規化します。
    // ±15%を可視化レンジの端に置き、それ以上は色を飽和させて外れ値で全体が見づらくなるのを防ぎます。
    float normalizedDensityDeviation = 0.0f;
    if (settings.RestDensity > math::Epsilon)
    {
        const float relativeDensityDeviation =
            (particle.Density - settings.RestDensity) / settings.RestDensity;
        normalizedDensityDeviation = std::clamp(
            relativeDensityDeviation / DensityVisualizationRange,
            -1.0f,
            1.0f);
    }

    // PressureはEOSの係数が変わっても同じ色レンジで比較できるよう、
    // 「RestDensityから15%ずれたときのPressure」を基準値にします。
    float normalizedPressure = 0.0f;
    const float pressureReference =
        settings.PressureStiffness
        * settings.RestDensity
        * DensityVisualizationRange;
    if (pressureReference > math::Epsilon)
    {
        normalizedPressure = std::clamp(
            particle.Pressure / pressureReference,
            -1.0f,
            1.0f);
    }

    // 現在の線形EOSではDensity偏差とPressureはほぼ同じ情報ですが、
    // hueはPressure、明るさはDensityへ分けておくと、将来Tait EOS等へ変更したときも
    // 「圧力」と「密度」の違いを同じ可視化関数で表現できます。
    math::Vec3 color = RestDensityColor;
    if (normalizedPressure < 0.0f)
    {
        color = LerpColor(RestDensityColor, LowDensityColor, -normalizedPressure);
    }
    else
    {
        color = LerpColor(RestDensityColor, HighDensityColor, normalizedPressure);
    }

    const float densityBrightness = std::clamp(
        0.90f + 0.10f * normalizedDensityDeviation,
        0.80f,
        1.00f);
    return color * densityBrightness;
}

void FluidSPHDemoLayer::SynchronizeRenderEntities()
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr)
    {
        return;
    }

    const std::size_t count = std::min(
        m_Particles.size(),
        std::min(m_ParticleEntities.size(), m_ParticleMaterials.size()));
    for (std::size_t i = 0u; i < count; ++i)
    {
        Entity& entity = m_ParticleEntities[i];
        if (static_cast<bool>(entity) == false || scene->IsEntityAlive(entity) == false)
        {
            continue;
        }

        TransformComponent& transform = entity.GetComponent<TransformComponent>();
        transform.Position = m_Particles[i].Position;
        transform.Scale = math::Vec3(RenderParticleRadius * 2.0f);

        Ref<Material>& material = m_ParticleMaterials[i];
        if (material != nullptr)
        {
            material->SetUniform("u_Tint", ComputeParticleDebugColor(m_Particles[i]));
        }
    }
}

} // namespace Raven
