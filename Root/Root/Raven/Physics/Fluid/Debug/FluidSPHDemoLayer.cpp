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

// Terrainが存在する原点周辺を避け、Fluid検証専用エリアを(+100, +100)側へ分離します。
// Particle / SPH Boundary / 水槽Collider / 落下Bodyはすべてこの基準位置から配置します。
constexpr math::Vec3 FluidDemoCenter{ 100.0f, 4.0f, 100.0f };
constexpr math::Vec3 FluidOrigin = FluidDemoCenter + math::Vec3{ -0.8f, 0.0f, -0.8f };
constexpr math::Vec3 BoundaryMinimum = FluidDemoCenter + math::Vec3{ -4.0f, -3.8f, -4.0f };
constexpr math::Vec3 BoundaryMaximum = FluidDemoCenter + math::Vec3{ 4.0f, 4.0f, 4.0f };
constexpr float TankHalfWidth = 4.0f;
constexpr float TankWallThickness = 0.20f;
constexpr float TankHeight = 7.8f;
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

    // Static / Dynamic Couplingは同じParticle半径と反発係数を使用します。
    // 接触応答の設定をSPHSolverへ混ぜず、Domain間Coupling固有値として分離します。
    ph::FluidStaticColliderCouplingSettings staticCouplingSettings{};
    staticCouplingSettings.ParticleRadius = RenderParticleRadius;
    staticCouplingSettings.Restitution = initialSettings.BoundaryRestitution;
    m_StaticColliderCoupling.SetSettings(staticCouplingSettings);

    ph::FluidRigidBodyCouplingSettings rigidBodyCouplingSettings{};
    rigidBodyCouplingSettings.ParticleRadius = RenderParticleRadius;
    rigidBodyCouplingSettings.Restitution = initialSettings.BoundaryRestitution;
    rigidBodyCouplingSettings.DragCoefficient = 0.15f;
    rigidBodyCouplingSettings.PressureReactionCoefficient = 1.0f;
    rigidBodyCouplingSettings.BuoyancyCoefficient = 1.0f;
    m_RigidBodyCoupling.SetSettings(rigidBodyCouplingSettings);

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
    m_DemoCubeMesh = PrimitiveMeshFactory::CreateCube();
    if (m_ParticleMesh == nullptr || m_DemoCubeMesh == nullptr)
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
        m_DemoCubeMesh.reset();
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

    m_ParticlePipeline = Pipeline::Create(pipelineSpecification);
    if (m_ParticlePipeline == nullptr)
    {
        m_ParticleMesh.reset();
        m_DemoCubeMesh.reset();
        return;
    }

    CreateRenderEntities();
    CreateDemoTank();
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
        for (Entity& entity : m_DemoEntities)
        {
            if (static_cast<bool>(entity) && scene->IsEntityAlive(entity))
            {
                scene->DestroyEntity(entity);
            }
        }
    }

    m_ParticleEntities.clear();
    m_DemoEntities.clear();
    m_ParticleMaterials.clear();
    m_Particles.clear();
    m_ParticlePipeline.reset();
    m_DemoCubeMesh.reset();
    m_ParticleMesh.reset();
}

void FluidSPHDemoLayer::OnUpdate(float deltaTime)
{
    if (m_Particles.empty())
    {
        return;
    }

    const float safeDeltaTime = std::clamp(deltaTime, 0.0f, 0.0333333f);
    if (safeDeltaTime <= 0.0f)
    {
        return;
    }

    m_Solver.Step(m_Particles, safeDeltaTime);

    Scene* scene = m_Application.GetScene();
    if (scene != nullptr)
    {
        m_StaticColliderCoupling.ResolveScene(*scene, m_Particles);
        m_RigidBodyCoupling.ResolveScene(
            *scene,
            scene->GetPhysicsWorld(),
            m_Particles,
            safeDeltaTime);
    }

    SynchronizeRenderEntities();
}

void FluidSPHDemoLayer::OnRender()
{
    // Particleと水槽は通常のMeshRendererComponentとしてSceneへ登録済みです。
}

void FluidSPHDemoLayer::CreateParticles()
{
    m_Particles.clear();
    m_Particles.reserve(static_cast<std::size_t>(ParticleCountX) * static_cast<std::size_t>(ParticleCountY) * static_cast<std::size_t>(ParticleCountZ));

    for (uint32_t y = 0u; y < ParticleCountY; ++y)
    {
        for (uint32_t z = 0u; z < ParticleCountZ; ++z)
        {
            for (uint32_t x = 0u; x < ParticleCountX; ++x)
            {
                ph::FluidParticle particle{};
                particle.Position = FluidOrigin + math::Vec3{ static_cast<float>(x) * ParticleSpacing, static_cast<float>(y) * ParticleSpacing, static_cast<float>(z) * ParticleSpacing };
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
        Entity entity = scene->CreateEntity("Fluid Particle");
        entity.GetComponent<TransformComponent>().Scale = { RenderParticleRadius, RenderParticleRadius, RenderParticleRadius };

        Ref<Material> material = CreateRef<Material>(m_ParticlePipeline);
        // test.fragはTint(vec3)とAlpha(float)を別Uniformとして受け取ります。
        // Vec4をu_Tintへ渡すとu_Alphaが設定されず透明になるため、型をShader契約へ合わせます。
        material->SetUniform("u_Alpha", 0.72f);
        entity.AddComponent<MeshRendererComponent>(
            MeshRendererComponent{ m_ParticleMesh, material });

        m_ParticleEntities.push_back(entity);
        m_ParticleMaterials.push_back(material);
    }
}

void FluidSPHDemoLayer::CreateDemoTank()
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr || m_DemoCubeMesh == nullptr || m_ParticlePipeline == nullptr)
    {
        return;
    }

    m_DemoEntities.clear();

    Ref<Material> tankMaterial = CreateRef<Material>(m_ParticlePipeline);
    tankMaterial->SetUniform("u_Tint", math::Vec3{ 0.20f, 0.65f, 0.85f });
    tankMaterial->SetUniform("u_Alpha", 0.35f);

    auto createStaticWall = [&](const char* name, const math::Vec3& position, const math::Vec3& scale)
    {
        Entity wall = scene->CreateEntity(name);
        TransformComponent& transform = wall.GetComponent<TransformComponent>();
        transform.Position = position;
        transform.Scale = scale;
        wall.AddComponent<MeshRendererComponent>(MeshRendererComponent{ m_DemoCubeMesh, tankMaterial });

        ColliderComponent collider{};
        collider.Type = ColliderType::Box;
        collider.HalfExtents = scale * 0.5f;
        collider.Restitution = 0.05f;
        collider.StaticFriction = 0.4f;
        collider.DynamicFriction = 0.2f;
        wall.AddComponent<ColliderComponent>(collider);
        m_DemoEntities.push_back(wall);
    };

    const float tankCenterY = (BoundaryMinimum.y + BoundaryMaximum.y) * 0.5f;
    createStaticWall("Fluid Tank Floor", { FluidDemoCenter.x, BoundaryMinimum.y - TankWallThickness * 0.5f, FluidDemoCenter.z }, { TankHalfWidth * 2.0f, TankWallThickness, TankHalfWidth * 2.0f });
    createStaticWall("Fluid Tank Wall -X", { BoundaryMinimum.x - TankWallThickness * 0.5f, tankCenterY, FluidDemoCenter.z }, { TankWallThickness, TankHeight, TankHalfWidth * 2.0f });
    createStaticWall("Fluid Tank Wall +X", { BoundaryMaximum.x + TankWallThickness * 0.5f, tankCenterY, FluidDemoCenter.z }, { TankWallThickness, TankHeight, TankHalfWidth * 2.0f });
    createStaticWall("Fluid Tank Wall -Z", { FluidDemoCenter.x, tankCenterY, BoundaryMinimum.z - TankWallThickness * 0.5f }, { TankHalfWidth * 2.0f, TankHeight, TankWallThickness });
    createStaticWall("Fluid Tank Wall +Z", { FluidDemoCenter.x, tankCenterY, BoundaryMaximum.z + TankWallThickness * 0.5f }, { TankHalfWidth * 2.0f, TankHeight, TankWallThickness });

    Ref<Material> bodyMaterial = CreateRef<Material>(m_ParticlePipeline);
    bodyMaterial->SetUniform("u_Tint", math::Vec3{ 1.0f, 0.55f, 0.10f });
    bodyMaterial->SetUniform("u_Alpha", 1.0f);

    Entity body = scene->CreateEntity("Fluid Buoyancy Test Body");
    TransformComponent& bodyTransform = body.GetComponent<TransformComponent>();
    bodyTransform.Position = FluidDemoCenter + math::Vec3{ 0.0f, 3.0f, 0.0f };
    bodyTransform.Scale = { 0.8f, 0.8f, 0.8f };
    body.AddComponent<MeshRendererComponent>(MeshRendererComponent{ m_DemoCubeMesh, bodyMaterial });

    RigidBodyComponent rigidBody{};
    rigidBody.SetBodyType(BodyType::Dynamic);
    rigidBody.SetMass(2.0f);
    rigidBody.LinearDamping = 0.02f;
    rigidBody.AngularDamping = 0.05f;
    rigidBody.UseGravity = true;
    rigidBody.AllowSleep = false;
    body.AddComponent<RigidBodyComponent>(rigidBody);

    ColliderComponent bodyCollider{};
    bodyCollider.Type = ColliderType::Box;
    bodyCollider.HalfExtents = bodyTransform.Scale * 0.5f;
    bodyCollider.Restitution = 0.05f;
    bodyCollider.StaticFriction = 0.4f;
    bodyCollider.DynamicFriction = 0.2f;
    body.AddComponent<ColliderComponent>(bodyCollider);
    m_DemoEntities.push_back(body);
}

void FluidSPHDemoLayer::SynchronizeRenderEntities()
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr)
    {
        return;
    }

    const std::size_t count = std::min(m_Particles.size(), m_ParticleEntities.size());
    const float restDensity = std::max(m_Solver.GetSettings().RestDensity, math::Epsilon);
    for (std::size_t i = 0u; i < count; ++i)
    {
        Entity& entity = m_ParticleEntities[i];
        if (static_cast<bool>(entity) == false || scene->IsEntityAlive(entity) == false)
        {
            continue;
        }

        entity.GetComponent<TransformComponent>().Position = m_Particles[i].Position;

        if (i >= m_ParticleMaterials.size() || m_ParticleMaterials[i] == nullptr)
        {
            continue;
        }

        const float normalizedDensity = (m_Particles[i].Density - restDensity) / (restDensity * DensityVisualizationRange);
        const float positive = std::clamp(normalizedDensity, 0.0f, 1.0f);
        const float negative = std::clamp(-normalizedDensity, 0.0f, 1.0f);
        math::Vec3 color = RestDensityColor;
        if (normalizedDensity >= 0.0f)
        {
            color = LerpColor(RestDensityColor, HighDensityColor, positive);
        }
        else
        {
            color = LerpColor(RestDensityColor, LowDensityColor, negative);
        }
        m_ParticleMaterials[i]->SetUniform("u_Tint", color);
        m_ParticleMaterials[i]->SetUniform("u_Alpha", 0.72f);
    }
}

} // namespace Raven
