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

// Terrainが存在する原点周辺を避け、Fluid検証専用エリアを(+50, +50)側へ分離します。
// Particle / SPH Boundary / 水槽Collider / 落下Bodyはすべてこの基準位置から配置します。
// 個別の座標を直接50付近へずらすのではなく共通基準から導出し、将来デモ位置を変更した場合も
// Simulation境界と可視化水槽、Coupling対象Bodyが互いにずれないようにします。
constexpr math::Vec3 FluidDemoCenter{ 50.0f, 4.0f, 50.0f };
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
    // Demoでは弱めのDragを有効にし、RigidBody表面をFluidが完全に滑り抜ける状態を避けます。
    rigidBodyCouplingSettings.DragCoefficient = 0.15f;
    // Particleの正圧を代表投影面積へ作用させ、RigidBodyへ面圧反作用として返します。
    rigidBodyCouplingSettings.PressureReactionCoefficient = 1.0f;
    // 接触Particleの排除質量からArchimedes相当の浮力を構築します。
    // 係数1.0を基準に、RigidBody質量と排除Fluid質量の比で浮く/沈む挙動が変わります。
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

    // GPU PipelineはParticle / 水槽 / Coupling確認Bodyで共有します。
    // Particleは密度ごとにu_Tintが変化するためMaterialだけを個別に持ち、色状態が別Particleへ漏れないようにします。
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
        // Demo Layerが生成したEntityだけを明示的に破棄し、Active Scene側へ検証用Entityを残しません。
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
        // StaticはParticleだけを補正し、Dynamic RigidBodyにはNewtonの第三法則に従って
        // Normal / Pressure / Buoyancy / Dragの運動量交換を返します。
        // SPHSolver自体にはScene依存を持たせず、異なるPhysics Domain間の接続はCoupling層へ委譲します。
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
    // SceneGame::RenderScene()のECS描画経路へ自動参加するため、専用Render処理は不要です。
}

void FluidSPHDemoLayer::CreateParticles()
{
    m_Particles.clear();
    m_Particles.reserve(static_cast<std::size_t>(ParticleCountX) * static_cast<std::size_t>(ParticleCountY) * static_cast<std::size_t>(ParticleCountZ));

    // 初期状態は規則的な3次元格子にします。
    // FluidOrigin自体がFluidDemoCenter基準なので、Simulationを移動しても粒子配置の局所形状は変化しません。
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

        // RavenのMaterialはFactoryではなくコンストラクタでPipelineを受け取る設計です。
        // ParticleごとにMaterialを分離し、各Entityの密度可視化色を独立して保持します。
        Ref<Material> material = CreateRef<Material>(m_ParticlePipeline);
        // test.fragはTint(vec3)とAlpha(float)を別Uniformとして受け取ります。
        // Vec4をu_Tintへ渡すとu_Alphaが設定されず透明になるため、Alphaは必ず別Uniformへ設定します。
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

    // 水槽はFluidデモエリアを遠距離から見つけやすくする視覚ガイドでもあります。
    // test.fragのUniform契約に合わせてTintとAlphaを分離し、内部のParticleや落下Bodyを確認できる透明度にします。
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
        // Primitive Cubeは各軸[-0.5,+0.5]なので、見た目のScaleの半分をColliderへ設定します。
        // 描画壁とPhysics壁を同じTransformから構築し、見えている水槽面と接触位置のずれを避けます。
        collider.HalfExtents = scale * 0.5f;
        collider.Restitution = 0.05f;
        collider.StaticFriction = 0.4f;
        collider.DynamicFriction = 0.2f;
        wall.AddComponent<ColliderComponent>(collider);
        m_DemoEntities.push_back(wall);
    };

    const float tankCenterY = (BoundaryMinimum.y + BoundaryMaximum.y) * 0.5f;
    createStaticWall(
        "Fluid Tank Floor",
        { FluidDemoCenter.x, BoundaryMinimum.y - TankWallThickness * 0.5f, FluidDemoCenter.z },
        { TankHalfWidth * 2.0f, TankWallThickness, TankHalfWidth * 2.0f });
    createStaticWall(
        "Fluid Tank Wall -X",
        { BoundaryMinimum.x - TankWallThickness * 0.5f, tankCenterY, FluidDemoCenter.z },
        { TankWallThickness, TankHeight, TankHalfWidth * 2.0f });
    createStaticWall(
        "Fluid Tank Wall +X",
        { BoundaryMaximum.x + TankWallThickness * 0.5f, tankCenterY, FluidDemoCenter.z },
        { TankWallThickness, TankHeight, TankHalfWidth * 2.0f });
    createStaticWall(
        "Fluid Tank Wall -Z",
        { FluidDemoCenter.x, tankCenterY, BoundaryMinimum.z - TankWallThickness * 0.5f },
        { TankHalfWidth * 2.0f, TankHeight, TankWallThickness });
    createStaticWall(
        "Fluid Tank Wall +Z",
        { FluidDemoCenter.x, tankCenterY, BoundaryMaximum.z + TankWallThickness * 0.5f },
        { TankHalfWidth * 2.0f, TankHeight, TankWallThickness });

    // BoxとSphereを同じ高さから左右へ並べて落下させ、Collider形状が異なっても
    // 同じFluidRigidBodyCouplingから浮力・面圧・Dragを受けることを比較できるようにします。
    // 現在の浮力はParticle接触量から近似するため、厳密な排水体積比較ではなく挙動確認用です。
    constexpr float TestBodyMass = 0.50f;
    constexpr float TestBodyHeightOffset = 3.0f;

    Ref<Material> boxMaterial = CreateRef<Material>(m_ParticlePipeline);
    boxMaterial->SetUniform("u_Tint", math::Vec3{ 1.0f, 0.55f, 0.10f });
    boxMaterial->SetUniform("u_Alpha", 1.0f);

    Entity box = scene->CreateEntity("Fluid Buoyancy Test Box");
    TransformComponent& boxTransform = box.GetComponent<TransformComponent>();
    boxTransform.Position = FluidDemoCenter + math::Vec3{ -0.75f, TestBodyHeightOffset, 0.0f };
    boxTransform.Scale = { 0.8f, 0.8f, 0.8f };
    box.AddComponent<MeshRendererComponent>(MeshRendererComponent{ m_DemoCubeMesh, boxMaterial });

    RigidBodyComponent boxRigidBody{};
    boxRigidBody.SetBodyType(BodyType::Dynamic);
    boxRigidBody.SetMass(TestBodyMass);
    boxRigidBody.LinearDamping = 0.02f;
    boxRigidBody.AngularDamping = 0.05f;
    boxRigidBody.UseGravity = true;
    boxRigidBody.AllowSleep = false;
    box.AddComponent<RigidBodyComponent>(boxRigidBody);

    ColliderComponent boxCollider{};
    boxCollider.Type = ColliderType::Box;
    boxCollider.HalfExtents = boxTransform.Scale * 0.5f;
    boxCollider.Restitution = 0.05f;
    boxCollider.StaticFriction = 0.4f;
    boxCollider.DynamicFriction = 0.2f;
    box.AddComponent<ColliderComponent>(boxCollider);
    m_DemoEntities.push_back(box);

    Ref<Material> sphereMaterial = CreateRef<Material>(m_ParticlePipeline);
    sphereMaterial->SetUniform("u_Tint", math::Vec3{ 0.55f, 1.0f, 0.20f });
    sphereMaterial->SetUniform("u_Alpha", 1.0f);

    // PrimitiveMeshFactory::CreateSphere()は半径0.5のSphereです。
    // Scaleを直径として扱い、見た目の半径とCollider::Radiusを一致させます。
    constexpr float SphereDiameter = 0.90f;
    constexpr float SphereRadius = SphereDiameter * 0.5f;

    Entity sphere = scene->CreateEntity("Fluid Buoyancy Test Sphere");
    TransformComponent& sphereTransform = sphere.GetComponent<TransformComponent>();
    sphereTransform.Position = FluidDemoCenter + math::Vec3{ 0.75f, TestBodyHeightOffset, 0.0f };
    sphereTransform.Scale = { SphereDiameter, SphereDiameter, SphereDiameter };
    sphere.AddComponent<MeshRendererComponent>(MeshRendererComponent{ m_ParticleMesh, sphereMaterial });

    RigidBodyComponent sphereRigidBody{};
    sphereRigidBody.SetBodyType(BodyType::Dynamic);
    sphereRigidBody.SetMass(TestBodyMass);
    sphereRigidBody.LinearDamping = 0.02f;
    sphereRigidBody.AngularDamping = 0.05f;
    sphereRigidBody.UseGravity = true;
    sphereRigidBody.AllowSleep = false;
    sphere.AddComponent<RigidBodyComponent>(sphereRigidBody);

    ColliderComponent sphereCollider{};
    sphereCollider.Type = ColliderType::Sphere;
    sphereCollider.Radius = SphereRadius;
    sphereCollider.Restitution = 0.05f;
    sphereCollider.StaticFriction = 0.4f;
    sphereCollider.DynamicFriction = 0.2f;
    sphere.AddComponent<ColliderComponent>(sphereCollider);
    m_DemoEntities.push_back(sphere);
}

math::Vec3 FluidSPHDemoLayer::ComputeParticleDebugColor(
    const ph::FluidParticle& particle) const
{
    // Densityの絶対値ではなくRestDensityからの相対偏差を使います。
    // Demo起動時にRestDensityを初期格子から較正しているため、ParticleMassやSpacingを変更しても
    // 「基準密度=水色」という可視化の意味を維持できます。
    const float restDensity = std::max(m_Solver.GetSettings().RestDensity, math::Epsilon);
    const float visualizationRange = std::max(
        restDensity * DensityVisualizationRange,
        math::Epsilon);
    const float normalizedDensity =
        (particle.Density - restDensity) / visualizationRange;

    if (normalizedDensity >= 0.0f)
    {
        // 高密度側は圧縮の強さを水色 -> 赤で示します。
        return LerpColor(
            RestDensityColor,
            HighDensityColor,
            std::clamp(normalizedDensity, 0.0f, 1.0f));
    }

    // 低密度側は膨張/負圧側を水色 -> 青で示します。
    return LerpColor(
        RestDensityColor,
        LowDensityColor,
        std::clamp(-normalizedDensity, 0.0f, 1.0f));
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

        // Simulation Particleを描画Entityへ一方向同期します。
        // ECS TransformをSimulation入力に戻さないことで、SPHSolverをSceneから独立した状態に保ちます。
        entity.GetComponent<TransformComponent>().Position = m_Particles[i].Position;

        if (i >= m_ParticleMaterials.size() || m_ParticleMaterials[i] == nullptr)
        {
            continue;
        }

        // Density色の判定はComputeParticleDebugColor()へ集約します。
        // Synchronize側は「Simulation値をRendererへ転送する」責務だけを持ち、可視化規則の重複を避けます。
        const math::Vec3 color = ComputeParticleDebugColor(m_Particles[i]);
        m_ParticleMaterials[i]->SetUniform("u_Tint", color);
        m_ParticleMaterials[i]->SetUniform("u_Alpha", 0.72f);
    }
}

} // namespace Raven
