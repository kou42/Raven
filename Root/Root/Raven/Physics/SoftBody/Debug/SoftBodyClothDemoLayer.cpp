#include "Raven/Physics/SoftBody/Debug/SoftBodyClothDemoLayer.h"

#include <algorithm>
#include <filesystem>
#include <vector>

#include "Raven/Core/Application.h"
#include "Raven/Debug/BrowserDebugConfig.h"
#include "Raven/Physics/RigidSoftCouplingComponent.h"
#include "Raven/Physics/SoftBody/Debug/SoftBodyParticleTriangleCandidateDebugSnapshot.h"
#include "Raven/Physics/SoftBody/Debug/SoftBodyParticleTriangleCandidateDebugSvgWriter.h"
#include "Raven/Physics/SoftBody/Debug/SoftBodyPhysicsDebugSvgWriter.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"
#include "Raven/Renderer/Mesh/Deformation/SoftBodyClothDeformer.h"
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
constexpr uint32_t kClothRows = 24u;
constexpr uint32_t kClothColumns = 24u;

// Solverは小さなローカル座標で計算し、Entity TransformだけでWorldへ配置します。
constexpr math::Vec3 kClothWorldPosition{ 0.0f, 18.0f, -10.0f };
constexpr float kClothWorldScale = 22.0f;
constexpr float kFloorLocalY = -18.0f / 22.0f;

constexpr math::Vec3 kRigidSphereInitialLocalCenter{ 0.0f, 0.0f, 0.36f };
constexpr float kRigidSphereWorldRadius = 2.4f;
constexpr float kRigidSphereMass = 4.0f;
constexpr math::Vec3 kRigidSphereInitialVelocity{ 0.0f, 0.0f, -8.0f };

// SoftBody ParticleとRigidBodyの質量単位系はまだ完全には校正していないため、
// デモでは反作用ScaleとClampを永続Coupling設定として明示します。
constexpr float kSoftRigidReactionScale = 0.12f;
constexpr float kMaxReactionImpulse = 18.0f;

#ifdef _DEBUG
constexpr float kBrowserDebugWriteIntervalSeconds = 0.10f;
const std::filesystem::path kBrowserDebugSvgPath =
    std::filesystem::path("Raven") / "Debug" / "Generated" / "Startup.svg";
const std::filesystem::path kBrowserCandidateRejectSvgPath =
    std::filesystem::path("Raven") / "Debug" / "Generated" / "CandidateRejects.svg";
#endif

math::Vec3 ClothLocalToWorldPosition(const math::Vec3& localPosition)
{
    return kClothWorldPosition + localPosition * kClothWorldScale;
}

float WorldToClothLocalLength(float worldLength)
{
    return worldLength / kClothWorldScale;
}

#ifdef _DEBUG
void WriteBrowserDebugSnapshot(
    const SoftBodyClothDeformer& clothDeformer,
    const Ref<Mesh>& clothMesh)
{
    if (clothMesh == nullptr)
    {
        return;
    }

    const Ref<MeshGeometry>& geometry = clothMesh->GetGeometry();
    if (geometry == nullptr)
    {
        return;
    }

    const ph::SoftBodyCloth& cloth = clothDeformer.GetCloth();
    const std::vector<uint32_t>& meshIndices = geometry->GetIndices();

    std::vector<uint32_t> particleTriangleIndices;
    particleTriangleIndices.reserve(meshIndices.size());

    for (uint32_t meshVertexIndex : meshIndices)
    {
        if (meshVertexIndex >= cloth.ParticleIndices.size())
        {
            return;
        }

        particleTriangleIndices.push_back(cloth.ParticleIndices[meshVertexIndex]);
    }

    const ph::SoftBodySolver& solver = clothDeformer.GetSolver();

    std::vector<ph::SoftBodyTriangleSpatialHashCellDebugInfo> spatialHashCells;
    solver.CollectParticleTriangleSpatialHashDebugInfo(spatialHashCells);

    ph::SoftBodyPhysicsDebugSvgWriter::Write(
        kBrowserDebugSvgPath,
        solver.GetParticles(),
        particleTriangleIndices,
        solver.GetParticleTriangleCollisionStatistics(),
        clothDeformer.GetParticleTriangleSpatialHashCellSize(),
        spatialHashCells);

    std::vector<ph::SoftBodyTriangle> debugTriangles;
    debugTriangles.reserve(particleTriangleIndices.size() / 3u);

    for (std::size_t index = 0u; index + 2u < particleTriangleIndices.size(); index += 3u)
    {
        ph::SoftBodyTriangle triangle{};
        triangle.ParticleA = particleTriangleIndices[index];
        triangle.ParticleB = particleTriangleIndices[index + 1u];
        triangle.ParticleC = particleTriangleIndices[index + 2u];
        debugTriangles.push_back(triangle);
    }

    const float horizontalSpacing = 1.0f / static_cast<float>(kClothColumns);
    const float verticalSpacing = 1.0f / static_cast<float>(kClothRows);
    const float minimumSpacing = std::min(horizontalSpacing, verticalSpacing);
    const float particleTriangleThickness = minimumSpacing * 0.30f;

    ph::SoftBodyParticleTriangleCandidateDebugSnapshot candidateSnapshot{};
    ph::SoftBodyParticleTriangleCandidateDebugSnapshotBuilder::Build(
        solver.GetParticles(),
        debugTriangles,
        clothDeformer.GetParticleTriangleSpatialHashCellSize(),
        particleTriangleThickness,
        candidateSnapshot);

    ph::SoftBodyParticleTriangleCandidateDebugSvgWriter::Write(
        kBrowserCandidateRejectSvgPath,
        solver.GetParticles(),
        particleTriangleIndices,
        candidateSnapshot);
}
#endif
} // namespace

void SoftBodyClothDemoLayer::OnAttach()
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr)
    {
        return;
    }

    m_ClothMesh = PrimitiveMeshFactory::CreateDynamicGrid(
        static_cast<int>(kClothRows),
        static_cast<int>(kClothColumns));
    m_CollisionSphereMesh = PrimitiveMeshFactory::CreateSphere();

    if (m_ClothMesh == nullptr || m_CollisionSphereMesh == nullptr)
    {
        m_ClothMesh.reset();
        m_CollisionSphereMesh.reset();
        return;
    }

    ShaderLibrary shaderLibrary{};
    Ref<Shader> shader = shaderLibrary.Load(
        "SoftBodyClothDemo",
        "Raven/Assets/Shaders/Vertex/test.vert",
        "Raven/Assets/Shaders/Fragment/test.frag");

    if (shader == nullptr)
    {
        m_ClothMesh.reset();
        m_CollisionSphereMesh.reset();
        return;
    }

    PipelineSpecification pipelineSpecification{};
    pipelineSpecification.DebugName = "SoftBody Cloth Demo Pipeline";
    pipelineSpecification.Shader = shader;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.DepthCompare = DepthCompareOperator::Less;
    pipelineSpecification.Blend = true;

    m_ClothMaterial = CreateRef<Material>(Pipeline::Create(pipelineSpecification));
    m_ClothMaterial->SetUniform("u_Tint", math::Vec3{ 0.35f, 0.65f, 1.0f });
    m_ClothMaterial->SetUniform("u_Alpha", 1.0f);

    pipelineSpecification.DebugName = "SoftBody Coupling Sphere Pipeline";
    m_RigidSphereMaterial = CreateRef<Material>(Pipeline::Create(pipelineSpecification));
    m_RigidSphereMaterial->SetUniform("u_Tint", math::Vec3{ 0.95f, 0.45f, 0.20f });
    m_RigidSphereMaterial->SetUniform("u_Alpha", 1.0f);

    // ========================================================================
    // Cloth Entity
    // ========================================================================
    m_ClothEntity = scene->CreateEntity("XPBD Cloth Demo");

    TransformComponent& clothTransform = m_ClothEntity.GetComponent<TransformComponent>();
    clothTransform.Position = kClothWorldPosition;
    clothTransform.Scale = { kClothWorldScale, kClothWorldScale, kClothWorldScale };

    m_ClothEntity.AddComponent<MeshRendererComponent>(
        MeshRendererComponent{ m_ClothMesh, m_ClothMaterial });

    auto clothDeformer = CreateScope<SoftBodyClothDeformer>(kClothRows, kClothColumns);
    clothDeformer->SetCollisionSphere(
        kRigidSphereInitialLocalCenter,
        WorldToClothLocalLength(kRigidSphereWorldRadius));
    clothDeformer->SetCollisionPlane({ 0.0f, 1.0f, 0.0f }, kFloorLocalY);

    m_ClothDeformationInstance = CreateRef<MeshDeformationInstance>(
        m_ClothMesh,
        std::move(clothDeformer));

    m_ClothEntity.AddComponent<MeshDeformationComponent>(
        MeshDeformationComponent{ m_ClothDeformationInstance, true });

    // ========================================================================
    // Dynamic RigidBody Sphere Entity
    // ========================================================================
    m_RigidSphereEntity = scene->CreateEntity("SoftBody Coupling Rigid Sphere");

    TransformComponent& sphereTransform = m_RigidSphereEntity.GetComponent<TransformComponent>();
    sphereTransform.Position = ClothLocalToWorldPosition(kRigidSphereInitialLocalCenter);

    const float sphereDiameter = kRigidSphereWorldRadius * 2.0f;
    sphereTransform.Scale = { sphereDiameter, sphereDiameter, sphereDiameter };

    m_RigidSphereEntity.AddComponent<MeshRendererComponent>(
        MeshRendererComponent{ m_CollisionSphereMesh, m_RigidSphereMaterial });

    RigidBodyComponent rigidBody{};
    rigidBody.SetBodyType(BodyType::Dynamic);
    rigidBody.SetMass(kRigidSphereMass);
    rigidBody.LinearVelocity = kRigidSphereInitialVelocity;
    rigidBody.LinearDamping = 0.02f;
    rigidBody.AngularDamping = 0.04f;
    rigidBody.UseGravity = true;
    rigidBody.AllowSleep = false;
    m_RigidSphereEntity.AddComponent<RigidBodyComponent>(rigidBody);

    ColliderComponent collider{};
    collider.Type = ColliderType::Sphere;
    collider.Radius = kRigidSphereWorldRadius;
    collider.Restitution = 0.15f;
    collider.StaticFriction = 0.5f;
    collider.DynamicFriction = 0.35f;
    m_RigidSphereEntity.AddComponent<ColliderComponent>(collider);

    // ========================================================================
    // Persistent Rigid <-> Soft coupling configuration
    // ========================================================================
    // LayerはSolver pointerやCollider Indexを保持しません。これらはMesh依存初期化後にしか
    // 確定しないRuntime情報なので、MeshDeformationSystemがこのComponentとDeformer共通境界から
    // 毎Game Update解決し、PhysicsSimulationWorldの非所有Binding Registryを再構築します。
    // そのため初回Physics Stepより前にBindingが成立し、OnDetachでの手動Unregisterも不要です。
    ph::RigidSoftCouplingComponent coupling{};
    coupling.SourceRigidEntity = m_RigidSphereEntity.GetHandle();
    coupling.Enabled = true;
    coupling.ReactionEnabled = true;
    coupling.ReactionImpulseScale = kSoftRigidReactionScale;
    coupling.MaximumReactionImpulse = kMaxReactionImpulse;
    m_ClothEntity.AddComponent<ph::RigidSoftCouplingComponent>(coupling);
}

void SoftBodyClothDemoLayer::OnDetach()
{
    Scene* scene = m_Application.GetScene();

    // Runtime Binding RegistryはMeshDeformationSystemがECSから毎frame再構築するため、
    // LayerはSolverへの非所有pointerを直接解除しません。Entityを破棄すれば次回再構築から自然に消えます。
    if (scene != nullptr)
    {
        if (static_cast<bool>(m_ClothEntity)
            && scene->IsEntityAlive(m_ClothEntity))
        {
            scene->DestroyEntity(m_ClothEntity);
        }

        if (static_cast<bool>(m_RigidSphereEntity)
            && scene->IsEntityAlive(m_RigidSphereEntity))
        {
            scene->DestroyEntity(m_RigidSphereEntity);
        }
    }

    m_ClothEntity = {};
    m_RigidSphereEntity = {};
    m_ClothDeformationInstance.reset();
    m_ClothMaterial.reset();
    m_RigidSphereMaterial.reset();
    m_CollisionSphereMesh.reset();
    m_ClothMesh.reset();
}

void SoftBodyClothDemoLayer::OnUpdate(float deltaTime)
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr
        || m_ClothDeformationInstance == nullptr
        || static_cast<bool>(m_ClothEntity) == false
        || scene->IsEntityAlive(m_ClothEntity) == false)
    {
        return;
    }

#ifdef _DEBUG
    MeshDeformer* baseDeformer = m_ClothDeformationInstance->GetDeformer();
    SoftBodyClothDeformer* clothDeformer = dynamic_cast<SoftBodyClothDeformer*>(baseDeformer);
    if (clothDeformer == nullptr)
    {
        return;
    }

    // CouplingのRuntime処理はSystem/Physicsへ移管済みなので、Application LayerのUpdateは
    // 完了済みSoftBody Stepを可視化するDebug Snapshotだけを担当します。
    if (kEnableBrowserDebugViewer == true)
    {
        static float browserDebugWriteAccumulator = kBrowserDebugWriteIntervalSeconds;
        browserDebugWriteAccumulator += std::max(deltaTime, 0.0f);

        if (browserDebugWriteAccumulator >= kBrowserDebugWriteIntervalSeconds)
        {
            browserDebugWriteAccumulator = 0.0f;
            WriteBrowserDebugSnapshot(*clothDeformer, m_ClothMesh);
        }
    }
#else
    static_cast<void>(deltaTime);
#endif
}

void SoftBodyClothDemoLayer::OnRender()
{
    // ClothとRigidBody Sphereは通常のMeshRendererComponentを持つため、描画はScene側へ一本化します。
    // Browser Debug ViewerもOnUpdate後に保存されたPhysics Snapshotを読み取るだけで、追加Render Passは持ちません。
}

} // namespace Raven
