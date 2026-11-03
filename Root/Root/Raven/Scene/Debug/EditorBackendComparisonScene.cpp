#include "Raven/Scene/Debug/EditorBackendComparisonScene.h"

#include "Raven/Core/Event.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/SceneCameraSystem.h"

#include <array>
#include <locale>
#include <sstream>

namespace Raven
{

EditorBackendComparisonScene::EditorBackendComparisonScene(LegacyMeshResourceCreation legacyCreation)
    : m_LegacyCreation(legacyCreation)
{
}

PipelineSpecification EditorBackendComparisonScene::GetPipelineSpecification()
{
    PipelineSpecification specification;
    specification.DebugName = "Editor Backend Comparison";
    specification.Topology = PrimitiveTopology::Triangles;
    specification.Cull = CullMode::None;
    specification.FrontFaceMode = FrontFace::CounterClockwise;
    specification.DepthTest = true;
    specification.DepthWrite = true;
    specification.DepthCompare = DepthCompareOperator::LessEqual;
    specification.Blend = false;
    return specification;
}

void EditorBackendComparisonScene::OnCreate()
{
    if (m_Ready == true)
    {
        return;
    }
    // Geometryは全Backendで同じFactoryから生成し、GPU Resourceの生成時期だけを分けます。
    const Ref<Mesh> cube = PrimitiveMeshFactory::CreateCube(m_LegacyCreation);
    const Ref<Mesh> floor = CreateRef<Mesh>(PrimitiveMeshFactory::CreateFloorGeometry(), m_LegacyCreation);
    if (cube == nullptr || floor == nullptr || cube->GetGeometry() == nullptr || floor->GetGeometry() == nullptr)
    {
        return;
    }
    Ref<Material> material;
    if (m_LegacyCreation == LegacyMeshResourceCreation::Immediate)
    {
        ShaderLibrary shaders;
        const Ref<Shader> shader = shaders.Load("EditorBackendComparison",
            "Raven/Assets/Shaders/Vertex/test.vert", "Raven/Assets/Shaders/Fragment/test.frag");
        if (shader == nullptr || cube->GetVertexArray() == nullptr || floor->GetVertexArray() == nullptr)
        {
            return;
        }
        PipelineSpecification pipeline = GetPipelineSpecification();
        pipeline.Shader = shader;
        material = CreateRef<Material>(Pipeline::Create(pipeline));
        if (material->HasLegacyPipeline() == false)
        {
            return;
        }
    }
    else
    {
        material = CreateRef<Material>();
    }
    // Legacy UniformとRHI Tintは別の契約です。両方を明示し、片方だけ既定値へ戻る差を防ぎます。
    // Textureは使用せず、Explicit側は既存Runtimeの白Texture fallbackを利用します。
    material->SetUniform("u_Tint", math::Vec3{1.0f, 1.0f, 1.0f});
    material->SetUniform("u_Alpha", 1.0f);
    material->SetRHITint({1.0f, 1.0f, 1.0f, 1.0f});
    material->SetSurfaceType(MaterialSurfaceType::Opaque);

    Entity camera = CreateEntity("Comparison Camera");
    auto& cameraTransform = camera.GetComponent<TransformComponent>();
    // Ravenは+Y up、Camera forwardは-Z、角度はradian。Scene Viewの既定Cameraと同じ姿勢です。
    cameraTransform.Position = {0.0f, 8.0f, 18.0f};
    cameraTransform.Rotation.x = -0.28f;
    CameraComponent cameraComponent;
    cameraComponent.Primary = true;
    cameraComponent.Camera.SetPerspective(0.78539816339f, 0.1f, 1000.0f);
    camera.AddComponent<CameraComponent>(cameraComponent);

    constexpr std::array<const char*, 3> names = {"Comparison Left", "Comparison Center", "Comparison Right"};
    constexpr std::array<float, 3> positions = {-3.0f, 0.0f, 3.0f};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        Entity entity = CreateEntity(names[index]);
        auto& transform = entity.GetComponent<TransformComponent>();
        // FactoryのCubeは各軸[-0.5,+0.5]。Scale 2で床上面Y=0に接する2m Cubeにします。
        transform.Position = {positions[index], 1.0f, 0.0f};
        transform.Scale = {2.0f, 2.0f, 2.0f};
        entity.AddComponent<MeshRendererComponent>(MeshRendererComponent{cube, material});
    }
    Entity floorEntity = CreateEntity("Comparison Floor");
    floorEntity.GetComponent<TransformComponent>().Scale = {16.0f, 1.0f, 12.0f};
    floorEntity.AddComponent<MeshRendererComponent>(MeshRendererComponent{floor, material});
    SceneCameraSystem::UpdatePrimaryCamera(*this, m_ViewportWidth, m_ViewportHeight);
    m_Ready = true;
}

void EditorBackendComparisonScene::OnDestroy()
{
    // EntityがMesh/Materialを共有所有します。既存Sceneの終了処理で全Componentを外してから
    // ApplicationがGPU Device/Contextを破棄するため、別の所有リストや非所有Entity参照は持ちません。
    Scene::OnDestroy();
    m_Ready = false;
}

bool EditorBackendComparisonScene::IsReady() const
{
    return m_Ready;
}

bool EditorBackendComparisonScene::ShouldUpdateSimulation() const
{
    // 比較中の編集結果をPhysics/Animationが書き戻さず、経過時間で描画負荷も変えません。
    return false;
}

void EditorBackendComparisonScene::OnRender()
{
    SceneCamera* camera = SceneCameraSystem::UpdatePrimaryCamera(*this, m_ViewportWidth, m_ViewportHeight);
    if (camera != nullptr)
    {
        RenderWithCamera(*camera);
    }
}

void EditorBackendComparisonScene::RenderWithCamera(const Camera& camera)
{
    if (m_Ready == false)
    {
        return;
    }
    if (Renderer::IsExplicitSceneMode() == false)
    {
        // ExplicitのClearは既存Runtimeが所有します。SceneからGL Clearを発行しません。
        RenderCommand::SetClearColor(0.1f, 0.1f, 0.3f, 1.0f);
        RenderCommand::Clear();
    }
    Renderer::BeginScene(camera);
    RenderEntities();
    Renderer::EndScene();
}

void EditorBackendComparisonScene::OnEvent(Event& event)
{
    if (event.GetEventType() == EventType::WindowResize)
    {
        const auto& resize = static_cast<const WindowResizeEvent&>(event);
        if (resize.GetWidth() > 0 && resize.GetHeight() > 0)
        {
            m_ViewportWidth = static_cast<float>(resize.GetWidth());
            m_ViewportHeight = static_cast<float>(resize.GetHeight());
        }
    }
    Scene::OnEvent(event);
}

void EditorBackendComparisonScene::WriteManifest(std::ostream& stream)
{
    // localeやGPU handleを含めず、起動時の論理データを同じ順序で出力します。
    std::ostringstream manifest;
    manifest.imbue(std::locale::classic());
    manifest << "[UI comparison] fixture=static-cubes-v1 units=meters up=+Y simulation=off\n";
    std::size_t entities = 0;
    std::size_t meshes = 0;
    for (auto [entity, tag, transform] : View<TagComponent, TransformComponent>())
    {
        ++entities;
        manifest << "[UI comparison] " << tag.Tag << " position=" << transform.Position.x << ','
            << transform.Position.y << ',' << transform.Position.z << " rotation=" << transform.Rotation.x << ','
            << transform.Rotation.y << ',' << transform.Rotation.z << " scale=" << transform.Scale.x << ','
            << transform.Scale.y << ',' << transform.Scale.z;
        if (entity.HasComponent<MeshRendererComponent>() == true)
        {
            ++meshes;
            const auto& renderer = entity.GetComponent<MeshRendererComponent>();
            const auto& tint = renderer.Material->GetRHITint();
            manifest << " vertices=" << renderer.Mesh->GetGeometry()->GetVertices().size()
                << " indices=" << renderer.Mesh->GetIndexCount() << " tint=" << tint[0] << ',' << tint[1]
                << ',' << tint[2] << ',' << tint[3];
        }
        if (entity.HasComponent<CameraComponent>() == true)
        {
            const auto& camera = entity.GetComponent<CameraComponent>().Camera;
            manifest << " fov=" << camera.GetPerspectiveVerticalFov() << " near=" << camera.GetPerspectiveNearClip()
                << " far=" << camera.GetPerspectiveFarClip() << " aspect=" << camera.GetAspectRatio();
        }
        manifest << '\n';
    }
    manifest << "[UI comparison] entities=" << entities << " meshes=" << meshes << '\n';
    stream << manifest.str();
}

} // namespace Raven
