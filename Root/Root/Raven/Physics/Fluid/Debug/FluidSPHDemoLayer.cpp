#include "Raven/Physics/Fluid/Debug/FluidSPHDemoLayer.h"

#include <algorithm>
#include <cstddef>

#include "Raven/Core/Application.h"
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
constexpr math::Vec3 FluidOrigin{ -0.8f, 4.0f, -0.8f };
constexpr math::Vec3 BoundaryMinimum{ -4.0f, 0.2f, -4.0f };
constexpr math::Vec3 BoundaryMaximum{ 4.0f, 8.0f, 4.0f };
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

    m_ParticleMaterial = CreateRef<Material>(Pipeline::Create(pipelineSpecification));
    m_ParticleMaterial->SetUniform("u_Tint", math::Vec3{ 0.20f, 0.55f, 1.0f });
    m_ParticleMaterial->SetUniform("u_Alpha", 1.0f);

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
    m_Particles.clear();
    m_ParticleMaterial.reset();
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
    if (scene == nullptr || m_ParticleMesh == nullptr || m_ParticleMaterial == nullptr)
    {
        return;
    }

    m_ParticleEntities.clear();
    m_ParticleEntities.reserve(m_Particles.size());
    for (std::size_t i = 0u; i < m_Particles.size(); ++i)
    {
        Entity entity = scene->CreateEntity("SPH Fluid Particle");
        entity.AddComponent<MeshRendererComponent>(
            MeshRendererComponent{ m_ParticleMesh, m_ParticleMaterial });
        m_ParticleEntities.push_back(entity);
    }
}

void FluidSPHDemoLayer::SynchronizeRenderEntities()
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr)
    {
        return;
    }

    const std::size_t count = std::min(m_Particles.size(), m_ParticleEntities.size());
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
    }
}

} // namespace Raven
