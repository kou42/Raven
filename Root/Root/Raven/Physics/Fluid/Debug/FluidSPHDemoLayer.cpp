#include "Raven/Physics/Fluid/Debug/FluidSPHDemoLayer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "Raven/Core/Application.h"
#include "Raven/Core/CPUProfiler.h"
#include "Raven/Math/Math.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{
namespace
{
constexpr uint32_t ParticleCountX = 15u;
// FluidOriginからBoundary上面までに収まる12層に制限します。
// 20層では上側7層(1,575 Particle)が初回Stepで同じ上面へclampされ、
// 密度・圧力・Neighbor候補とStable Substep数を不必要に増加させていました。
constexpr uint32_t ParticleCountY = 12u;
constexpr uint32_t ParticleCountZ = 15u;
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

math::Vec3 MultiplyColor(const math::Vec3& a, const math::Vec3& b)
{
    return { a.x * b.x, a.y * b.y, a.z * b.z };
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
    // Coupling設定はBindingへ保持し、実行自体はFluidWorldのFixed Stepへ一元化します。
    ph::FluidStaticColliderCouplingSettings staticCouplingSettings{};
    staticCouplingSettings.ParticleRadius = RenderParticleRadius;
    staticCouplingSettings.Restitution = initialSettings.BoundaryRestitution;
    m_CouplingBinding.StaticColliderSettings = staticCouplingSettings;

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
    m_CouplingBinding.RigidBodySettings = rigidBodyCouplingSettings;

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
    // Particleごとの密度色は結合Meshの頂点色へ格納し、MaterialとDraw Callは1つにまとめます。
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

    // Demo Layer自身を非所有Participantとして登録し、SPH Simulationを可変frame dtではなく
    // SceneのPhysics fixed-stepへ参加させます。所有権はLayer側に残るためDetach時に必ず解除します。
    scene->GetPhysicsSimulationWorld().GetFluidWorld().RegisterSimulationParticipant(*this);
}

void FluidSPHDemoLayer::OnDetach()
{
    Scene* scene = m_Application.GetScene();
    if (scene != nullptr)
    {
        // Layer破棄後のdangling pointerをFluidWorldへ残さないよう、Entity破棄より先にRegistryを解除します。
        scene->GetPhysicsSimulationWorld().GetFluidWorld().UnregisterSimulationParticipant(*this);

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
    m_ParticleTemplateVertices.clear();
    m_ParticleBatchVertices.clear();
    m_Particles.clear();
    m_ParticleBatchMaterial.reset();
    m_ParticleBatchMesh.reset();
    m_ParticlePipeline.reset();
    m_DemoCubeMesh.reset();
    m_ParticleMesh.reset();
}

void FluidSPHDemoLayer::OnUpdate(float deltaTime)
{
    // Fluid SimulationはPhysicsSimulationWorldのfixed-stepから実行します。
    // Layerの可変frame dtを使うとRigid/Soft/Thermalとの時間順序がframe rate依存になるため、
    // OnUpdateではSimulationやRender同期を行いません。
    (void)deltaTime;
}

void FluidSPHDemoLayer::OnActiveSceneChanging(Scene* scene)
{
    // 通知時点では旧Sceneがまだ生存しています。既存OnDetach経路を再利用し、
    // Entity / Physics Registry / GPU参照をScene破棄より先に解放します。
    if (scene == m_Application.GetScene())
    {
        OnDetach();
    }
}

void FluidSPHDemoLayer::OnActiveSceneChanged(Scene* scene)
{
    if (scene == nullptr || scene != m_Application.GetScene())
    {
        return;
    }

    // 新Active Sceneが確定した後に既存OnAttach経路でDemo状態を再構築します。
    OnAttach();
}

void FluidSPHDemoLayer::SimulateFluid(float fixedDeltaTime)
{
    if (m_Particles.empty())
    {
        return;
    }

    if (fixedDeltaTime <= 0.0f)
    {
        return;
    }

    // Fluid Participantは純粋な数値計算だけを担当します。
    // Static Collider / Dynamic RigidBody Couplingは、このStep直後にFluidWorldが一度だけ解決します。
    m_Solver.Step(m_Particles, fixedDeltaTime);
}

void FluidSPHDemoLayer::SynchronizeFluidOutput()
{
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
    m_ParticleTemplateVertices.clear();
    m_ParticleBatchVertices.clear();
    m_ParticleBatchMesh.reset();
    m_ParticleBatchMaterial.reset();

    const Ref<MeshGeometry>& sphereGeometry = m_ParticleMesh->GetGeometry();
    if (sphereGeometry == nullptr || sphereGeometry->GetVertices().empty()
        || sphereGeometry->GetIndices().empty())
    {
        return;
    }

    m_ParticleTemplateVertices = sphereGeometry->GetVertices();
    const std::vector<uint32_t>& sphereIndices = sphereGeometry->GetIndices();
    const std::size_t verticesPerParticle = m_ParticleTemplateVertices.size();

    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(m_Particles.size() * verticesPerParticle);
    indices.reserve(m_Particles.size() * sphereIndices.size());

    // Sphere topologyをParticle数だけ一度だけ複製します。以降は頂点Bufferをswapして再利用し、
    // Entity/MaterialをParticleごとに作る経路と毎frameのheap allocationを避けます。
    for (std::size_t particleIndex = 0u; particleIndex < m_Particles.size(); ++particleIndex)
    {
        const ph::FluidParticle& particle = m_Particles[particleIndex];
        const math::Vec3 tint = ComputeParticleDebugColor(particle);
        const uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());

        for (const MeshVertex& templateVertex : m_ParticleTemplateVertices)
        {
            MeshVertex vertex = templateVertex;
            vertex.Position = particle.Position + templateVertex.Position * RenderParticleRadius;
            vertex.Color = MultiplyColor(templateVertex.Color, tint);
            vertices.push_back(vertex);
        }
        for (uint32_t index : sphereIndices)
        {
            indices.push_back(vertexOffset + index);
        }
    }

    Ref<MeshGeometry> batchGeometry = CreateRef<MeshGeometry>(
        std::move(vertices),
        std::move(indices),
        GeometryUsage::Dynamic,
        TopologyUsage::Fixed);
    m_ParticleBatchMesh = CreateRef<Mesh>(batchGeometry);
    if (m_ParticleBatchMesh == nullptr)
    {
        return;
    }

    // SwapVertices()の相手側Buffer。初回だけ確保し、その後はGeometry側とcapacityを往復利用します。
    m_ParticleBatchVertices = batchGeometry->GetVertices();

    m_ParticleBatchMaterial = CreateRef<Material>(m_ParticlePipeline);
    m_ParticleBatchMaterial->SetSurfaceType(MaterialSurfaceType::Transparent);
    // Particle別Tintは頂点色へ焼き込むため、Material uniformは全体へ白を掛けます。
    m_ParticleBatchMaterial->SetUniform("u_Tint", math::Vec3{ 1.0f, 1.0f, 1.0f });
    m_ParticleBatchMaterial->SetUniform("u_Alpha", 0.72f);

    Entity entity = scene->CreateEntity("Fluid Particles");
    entity.AddComponent<MeshRendererComponent>(
        MeshRendererComponent{ m_ParticleBatchMesh, m_ParticleBatchMaterial });
    m_ParticleEntities.push_back(entity);
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
    // 水槽壁もOpacity値ではなくMaterialの描画意味としてTransparentへ分類します。
    // これにより将来ShaderのUniform名を変更してもTransparent Passへの登録規約は変わりません。
    tankMaterial->SetSurfaceType(MaterialSurfaceType::Transparent);
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
    RAVEN_PROFILE_SCOPE("Physics.Fluid.Demo.RenderBatchSync");

    Scene* scene = m_Application.GetScene();
    if (scene == nullptr || m_ParticleBatchMesh == nullptr
        || m_ParticleTemplateVertices.empty())
    {
        return;
    }

    if (m_ParticleEntities.empty()
        || static_cast<bool>(m_ParticleEntities.front()) == false
        || scene->IsEntityAlive(m_ParticleEntities.front()) == false)
    {
        return;
    }

    const std::size_t verticesPerParticle = m_ParticleTemplateVertices.size();
    const std::size_t expectedVertexCount = m_Particles.size() * verticesPerParticle;
    if (m_ParticleBatchVertices.size() != expectedVertexCount)
    {
        return;
    }

    // Simulation Particleを結合Meshへ一方向同期します。Topology、UV、Normalは不変なので、
    // hot pathではPositionとColorだけを書き換えます。
    for (std::size_t particleIndex = 0u; particleIndex < m_Particles.size(); ++particleIndex)
    {
        const ph::FluidParticle& particle = m_Particles[particleIndex];
        const math::Vec3 tint = ComputeParticleDebugColor(particle);
        const std::size_t vertexOffset = particleIndex * verticesPerParticle;

        for (std::size_t vertexIndex = 0u; vertexIndex < verticesPerParticle; ++vertexIndex)
        {
            const MeshVertex& templateVertex = m_ParticleTemplateVertices[vertexIndex];
            MeshVertex& vertex = m_ParticleBatchVertices[vertexOffset + vertexIndex];
            vertex.Position = particle.Position + templateVertex.Position * RenderParticleRadius;
            vertex.Color = MultiplyColor(templateVertex.Color, tint);
        }
    }

    const Ref<MeshGeometry>& batchGeometry = m_ParticleBatchMesh->GetGeometry();
    if (batchGeometry != nullptr && batchGeometry->SwapVertices(m_ParticleBatchVertices))
    {
        m_ParticleBatchMesh->SyncGeometry();
    }
}

} // namespace Raven
