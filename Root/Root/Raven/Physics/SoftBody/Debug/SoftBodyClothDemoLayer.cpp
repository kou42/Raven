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

void SoftBodyClothDemoLayer::OnAttach()
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr)
    {
        return;
    }

    // 24x24セル = 25x25 Particle。
    // 最初の検証として十分滑らかで、Constraint数もまだ軽量な規模です。
    constexpr uint32_t clothRows = 24u;
    constexpr uint32_t clothColumns = 24u;

    m_ClothMesh = PrimitiveMeshFactory::CreateDynamicGrid(
        static_cast<int>(clothRows),
        static_cast<int>(clothColumns));

    if (m_ClothMesh == nullptr)
    {
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

    // Scene側にはDeformation Componentだけを登録します。
    // SceneGame::OnUpdateGame()内のMeshDeformationSystem::Update()がこのEntityも自動走査するため、
    // SoftBody専用Update呼び出しをSceneGameへ追加する必要はありません。
    m_ClothEntity = scene->CreateEntity("XPBD Cloth Demo");

    auto clothDeformer = CreateScope<SoftBodyClothDeformer>(clothRows, clothColumns);
    clothDeformer->SetCollisionSphere({ 0.0f, -0.12f, 0.0f }, 0.20f);

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
    m_ClothMesh.reset();
}

void SoftBodyClothDemoLayer::OnRender()
{
    if (m_ClothMesh == nullptr || m_ClothMaterial == nullptr)
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

    // SoftBody SolverはClothローカル空間[-0.5,+0.5]で解いています。
    // 描画時だけWorldへ拡大・移動することでSolver数値スケールを安定したまま保ちます。
    math::Mat4 transform = math::Mat4::Identity();
    transform = math::Translate(transform, { 0.0f, 18.0f, -10.0f });
    transform = math::Scale(transform, { 22.0f, 22.0f, 22.0f });

    // SceneGame本体の描画後に追加Passとして描画します。
    // Clearは行わないため、既存SceneのColor/Depthを保持したままClothだけを重ねます。
    Renderer::BeginScene(*camera);
    Renderer::Draw(m_ClothMesh, m_ClothMaterial, transform);
    Renderer::EndScene();
}

} // namespace Raven
