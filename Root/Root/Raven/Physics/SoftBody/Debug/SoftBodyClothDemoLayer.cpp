#include "Raven/Physics/SoftBody/Debug/SoftBodyClothDemoLayer.h"

#include <cmath>
#include <filesystem>
#include <vector>

#include "Raven/Core/Application.h"
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
// ============================================================================
// XPBD Cloth Demo Parameters
// ============================================================================
constexpr uint32_t kClothRows = 24u;
constexpr uint32_t kClothColumns = 24u;
constexpr math::Vec3 kClothWorldPosition{ 0.0f, 18.0f, -10.0f };
constexpr float kClothWorldScale = 22.0f;
constexpr float kFloorLocalY = -18.0f / 22.0f;

constexpr math::Vec3 kRigidSphereInitialLocalCenter{ 0.0f, 0.0f, 0.36f };
constexpr float kRigidSphereWorldRadius = 2.4f;
constexpr float kRigidSphereMass = 4.0f;
constexpr math::Vec3 kRigidSphereInitialVelocity{ 0.0f, 0.0f, -8.0f };
constexpr float kSoftRigidReactionScale = 0.12f;
constexpr float kMaxReactionImpulse = 18.0f;

#ifdef _DEBUG
// Browser側は250ms間隔でSVGを再取得します。
// Raven側はそれより少し細かい100ms間隔でSnapshotを更新し、ブラウザの再取得時に
// ほぼ常に新しいSolver状態が存在するようにします。毎フレームofstreamを開く方式にしないことで、
// Physics / RendererのProfiler計測へファイルI/Oが混入する量も抑えます。
constexpr float kBrowserDebugWriteIntervalSeconds = 0.10f;
const std::filesystem::path kBrowserDebugSvgPath =
    std::filesystem::path("Raven") / "Debug" / "Generated" / "Startup.svg";
#endif

math::Vec3 ClothLocalToWorldPosition(const math::Vec3& localPosition)
{
    return kClothWorldPosition + localPosition * kClothWorldScale;
}

math::Vec3 WorldToClothLocalPosition(const math::Vec3& worldPosition)
{
    return (worldPosition - kClothWorldPosition) / kClothWorldScale;
}

float WorldToClothLocalLength(float worldLength)
{
    return worldLength / kClothWorldScale;
}

math::Vec3 ClampMagnitude(const math::Vec3& value, float maxMagnitude)
{
    const float lengthSq = value.LengthSq();
    const float maxMagnitudeSq = maxMagnitude * maxMagnitude;

    if (lengthSq <= maxMagnitudeSq || lengthSq <= math::Epsilon * math::Epsilon)
    {
        return value;
    }

    return value * (maxMagnitude / std::sqrt(lengthSq));
}

#ifdef _DEBUG
// ============================================================================
// WriteBrowserDebugSnapshot
// ============================================================================
// Mesh GeometryのIndexは「Cloth内の頂点Index」であり、SVG Writerが要求するのは
// Solver全体のParticle Indexです。現在はSolver::Clear()直後にClothを構築するため両者は
// 実質一致しますが、将来1 Solverへ複数SoftBodyを登録しても壊れないよう、必ず
// SoftBodyCloth::ParticleIndicesを通して明示的に変換します。
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
            // Topologyが不整合なSnapshotを部分的に書き出すより、このFrameは出力しない方が
            // Debug Viewer上で誤ったTriangleを正しい状態と誤認しにくくなります。
            return;
        }

        particleTriangleIndices.push_back(cloth.ParticleIndices[meshVertexIndex]);
    }

    const ph::SoftBodySolver& solver = clothDeformer.GetSolver();
    ph::SoftBodyPhysicsDebugSvgWriter::Write(
        kBrowserDebugSvgPath,
        solver.GetParticles(),
        particleTriangleIndices,
        solver.GetParticleTriangleCollisionStatistics(),
        clothDeformer.GetParticleTriangleSpatialHashCellSize());
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
    // Scene側にはSoftBody専用描画処理を増やさず、既存の
    // MeshRendererComponent + MeshDeformationComponentとして登録します。
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
}

void SoftBodyClothDemoLayer::OnDetach()
{
    Scene* scene = m_Application.GetScene();

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
        || static_cast<bool>(m_RigidSphereEntity) == false
        || scene->IsEntityAlive(m_RigidSphereEntity) == false)
    {
        return;
    }

    MeshDeformer* baseDeformer = m_ClothDeformationInstance->GetDeformer();
    SoftBodyClothDeformer* clothDeformer = dynamic_cast<SoftBodyClothDeformer*>(baseDeformer);
    if (clothDeformer == nullptr)
    {
        return;
    }

    ph::SoftBodySolver& solver = clothDeformer->GetSolver();
    const std::vector<ph::SoftBodySphereCollider>& sphereColliders = solver.GetSphereColliders();

    // ========================================================================
    // Soft Body -> Rigid Body reaction
    // ========================================================================
    if (sphereColliders.empty() == false)
    {
        const ph::SoftBodySphereCollider& softSphere = sphereColliders.front();

        if (softSphere.ContactCount > 0u)
        {
            math::Vec3 worldReactionImpulse =
                softSphere.AccumulatedReactionImpulse
                * (kClothWorldScale * kSoftRigidReactionScale);

            worldReactionImpulse = ClampMagnitude(worldReactionImpulse, kMaxReactionImpulse);

            const math::Vec3 worldContactPoint =
                ClothLocalToWorldPosition(softSphere.GetAverageContactPoint());

            if (worldReactionImpulse.LengthSq() > math::Epsilon * math::Epsilon)
            {
                scene->GetPhysicsWorld().AddImpulseAtPoint(
                    *scene,
                    m_RigidSphereEntity,
                    worldReactionImpulse,
                    worldContactPoint);
            }
        }
    }

    // ========================================================================
    // Rigid Body -> Soft Body collider synchronization
    // ========================================================================
    const TransformComponent& sphereTransform =
        m_RigidSphereEntity.GetComponent<TransformComponent>();
    const ColliderComponent& sphereCollider =
        m_RigidSphereEntity.GetComponent<ColliderComponent>();

    const math::Vec3 localSphereCenter =
        WorldToClothLocalPosition(sphereTransform.Position + sphereCollider.Offset);
    const float localSphereRadius = WorldToClothLocalLength(sphereCollider.Radius);

    clothDeformer->SetCollisionSphere(localSphereCenter, localSphereRadius);

#ifdef _DEBUG
    // ========================================================================
    // Runtime SoftBody -> Browser Debug Viewer
    // ========================================================================
    // Application Layer::OnUpdate()はScene更新後なので、この時点のSolverは当該FrameのCloth Stepを
    // 完了しています。したがってParticle位置とFunnel Counterを同一SnapshotとしてSVGへ保存できます。
    // static accumulatorはこのDebug Demo Layerが1個だけ生成される現在の構成に限定した簡易Throttleです。
    static float browserDebugWriteAccumulator = kBrowserDebugWriteIntervalSeconds;
    browserDebugWriteAccumulator += std::max(deltaTime, 0.0f);

    if (browserDebugWriteAccumulator >= kBrowserDebugWriteIntervalSeconds)
    {
        browserDebugWriteAccumulator = 0.0f;
        WriteBrowserDebugSnapshot(*clothDeformer, m_ClothMesh);
    }
#endif
}

void SoftBodyClothDemoLayer::OnRender()
{
    // ClothとRigidBody Sphereは通常のMeshRendererComponentを持つため、描画はScene側へ一本化します。
    // Browser Debug Viewerも描画PassではなくOnUpdate後のPhysics Snapshotを読むため、ここでは何もしません。
}

} // namespace Raven
