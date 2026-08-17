#include "Raven/Physics/SoftBody/Debug/SoftBodyClothDemoLayer.h"

#include "Raven/Core/Application.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"
#include "Raven/Renderer/Mesh/Deformation/SoftBodyClothDeformer.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneCameraSystem.h"

namespace Raven
{
namespace
{
constexpr uint32_t kClothRows = 24u;
constexpr uint32_t kClothColumns = 24u;
constexpr math::Vec3 kCollisionSphereCenter{ 0.0f, -0.12f, 0.0f };
constexpr float kCollisionSphereRadius = 0.20f;
constexpr math::Vec3 kClothWorldPosition{ 0.0f, 18.0f, -10.0f };
constexpr float kClothWorldScale = 22.0f;

// SceneGameの床はWorld Y=0です。
// SolverはClothローカル空間で動くため、worldY = localY * scale + translationY を逆変換し、
// localY = (0 - 18) / 22 として同じ床面をSoftBody Solverへ登録します。
constexpr float kFloorLocalY = -18.0f / 22.0f;
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
    m_ClothMaterial->SetUniform("u_Alpha", 1.0f);

    m_ClothEntity = scene->CreateEntity("XPBD Cloth Demo");

    auto clothDeformer = CreateScope<SoftBodyClothDeformer>(kClothRows, kClothColumns);
    clothDeformer->SetCollisionSphere(kCollisionSphereCenter, kCollisionSphereRadius);

    // +Yを外側とするPlaneをScene床と同じWorld Y=0へ配置します。
    // ClothがSphereを滑り落ちた後も床より下へ抜けないことを目視確認できます。
    clothDeformer->SetCollisionPlane({ 0.0f, 1.0f, 0.0f }, kFloorLocalY);

    auto deformationInstance = std::make_shared<MeshDeformationInstance>(
        m_ClothMesh,
        std::move(clothDeformer));

    m_ClothEntity.AddComponent<MeshDeformationComponent>(
        MeshDeformationComponent{ std::move(deformationInstance), true });
}

void SoftBodyClothDemoLayer::OnDetach()
{
    Scene* scene = m_Application.GetScene();
    if (scene != nullptr
        && static_cast<bool>(m_ClothEntity)
        && scene->IsEntityAlive(m_ClothEntity))
    {
        scene->DestroyEntity(m_ClothEntity);
    }

    m_ClothEntity = {};
    m_ClothMaterial.reset();
    m_CollisionSphereMesh.reset();
    m_ClothMesh.reset();
}

void SoftBodyClothDemoLayer::OnRender()
{
    if (m_ClothMesh == nullptr
        || m_CollisionSphereMesh == nullptr
        || m_ClothMaterial == nullptr)
    {
        return;
    }

    Scene* scene = m_Application.GetScene();
    if (scene == nullptr)
    {
        return;
    }

    const Window& window = m_Application.GetWindow();
    SceneCamera* camera = SceneCameraSystem::UpdatePrimaryCamera(
        *scene,
        static_cast<float>(window.GetWidth()),
        static_cast<float>(window.GetHeight()));

    if (camera == nullptr)
    {
        return;
    }

    math::Mat4 clothTransform = math::Mat4::Identity();
    clothTransform = math::Translate(clothTransform, kClothWorldPosition);
    clothTransform = math::Scale(
        clothTransform,
        { kClothWorldScale, kClothWorldScale, kClothWorldScale });

    const math::Vec3 sphereWorldCenter =
        kClothWorldPosition + kCollisionSphereCenter * kClothWorldScale;
    const float sphereWorldDiameter =
        kCollisionSphereRadius * 2.0f * kClothWorldScale;

    math::Mat4 sphereTransform = math::Mat4::Identity();
    sphereTransform = math::Translate(sphereTransform, sphereWorldCenter);
    sphereTransform = math::Scale(
        sphereTransform,
        { sphereWorldDiameter, sphereWorldDiameter, sphereWorldDiameter });

    Renderer::BeginScene(*camera);

    m_ClothMaterial->SetUniform("u_Tint", math::Vec3{ 0.95f, 0.45f, 0.20f });
    Renderer::Draw(m_CollisionSphereMesh, m_ClothMaterial, sphereTransform);

    m_ClothMaterial->SetUniform("u_Tint", math::Vec3{ 0.35f, 0.65f, 1.0f });
    Renderer::Draw(m_ClothMesh, m_ClothMaterial, clothTransform);

    Renderer::EndScene();
}

} // namespace Raven
