#include "Raven/Editor/EditorLayer.h"

#include "Raven/Core/Application.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathUtility.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <cassert>

namespace Raven
{

void EditorLayer::RenderSelectionOutlinePass(
    Framebuffer& framebuffer,
    const Camera& camera)
{
    static_cast<void>(framebuffer);

    if (m_Application == nullptr)
    {
        return;
    }

    Scene* activeScene = m_Application->GetScene();
    if (activeScene == nullptr)
    {
        return;
    }

    // SelectionはHierarchy / Inspector / Scene Viewで共有しているEditor状態です。
    // Invalid Entityや別SceneのEntityを描画しないよう、ここでもSceneと生存状態を明示的に検証します。
    if (static_cast<bool>(m_SelectedEntity) == false
        || m_SelectedEntity.GetScene() != activeScene
        || activeScene->IsEntityAlive(m_SelectedEntity) == false)
    {
        return;
    }

    if (m_SelectedEntity.HasComponent<TransformComponent>() == false
        || m_SelectedEntity.HasComponent<MeshRendererComponent>() == false)
    {
        // Camera Entity等、Meshを持たないEntityはHierarchy/Inspectorでは選択可能ですが、
        // GeometryがないためOutline描画対象にはしません。
        return;
    }

    const TransformComponent& transform =
        m_SelectedEntity.GetComponent<TransformComponent>();
    const MeshRendererComponent& meshRenderer =
        m_SelectedEntity.GetComponent<MeshRendererComponent>();

    if (meshRenderer.IsValid() == false)
    {
        return;
    }

    // ========================================================================
    // Lazy creation of Editor-only outline material
    // ========================================================================
    // 通常MaterialへOutline用Stateや色を追加せず、Editor専用Materialを1つだけ共有します。
    // Inverted Hull方式ではFront FaceをCullして、少し拡大したMeshのBack Faceだけを描画します。
    // 元Meshが通常描画済みなので、視覚的にはGeometryの外周だけが単色で残ります。
    if (m_SelectionOutlineMaterial == nullptr)
    {
        Ref<Shader> outlineShader = Shader::Create(
            "Raven/Assets/Shaders/Glsl/SelectionOutline.glsl");

        if (outlineShader == nullptr)
        {
            assert(false && "Failed to create Selection Outline shader.");
            return;
        }

        PipelineSpecification pipelineSpecification{};
        pipelineSpecification.DebugName = "Editor Selection Outline Pipeline";
        pipelineSpecification.Shader = outlineShader;
        pipelineSpecification.Topology = PrimitiveTopology::Triangles;

        // Inverted Hullの核心です。
        // 拡大MeshのFront Faceを捨ててBack Faceだけを描くことで、元Mesh外周へはみ出した部分が
        // Outlineとして見えるようにします。Stencil State追加なしでEditor選択表示を実現できます。
        pipelineSpecification.Cull = CullMode::Front;
        pipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;

        // 通常SceneのDepth Bufferを再利用します。
        // Outline自身はDepthを書き換えず、他のGeometryに隠れている部分は描画しません。
        pipelineSpecification.DepthTest = true;
        pipelineSpecification.DepthWrite = false;
        pipelineSpecification.DepthCompare = DepthCompareOperator::LessEqual;
        pipelineSpecification.Blend = false;

        Ref<Pipeline> outlinePipeline = Pipeline::Create(pipelineSpecification);
        if (outlinePipeline == nullptr)
        {
            assert(false && "Failed to create Selection Outline pipeline.");
            return;
        }

        m_SelectionOutlineMaterial = CreateRef<Material>(outlinePipeline);

        // Editor Selection ColorはゲームMaterialとは独立した固定色にします。
        // 将来Editor Themeへ移す場合も、このUniform設定だけをTheme値へ差し替えられます。
        m_SelectionOutlineMaterial->SetUniform(
            "u_OutlineColor",
            math::Vec3{ 1.0f, 0.55f, 0.05f });
    }

    // ========================================================================
    // Inverted Hull transform
    // ========================================================================
    // TransformComponent::GetTransform()でEntity本来のTranslation/Rotation/Scaleを適用した後、
    // Local Spaceで少しだけ均一拡大します。
    // World SpaceでScaleするとTranslationまで拡大されてEntity位置がずれるため、Model Matrixへ
    // 後掛けすることが重要です。
    math::Mat4 outlineTransform = transform.GetTransform();
    outlineTransform = math::Scale(
        outlineTransform,
        math::Vec3{ 1.035f, 1.035f, 1.035f });

    m_SelectionOutlineMaterial->SetUniform(
        "u_View",
        camera.GetViewMatrix());
    m_SelectionOutlineMaterial->SetUniform(
        "u_Projection",
        camera.GetProjectionMatrix());
    m_SelectionOutlineMaterial->SetUniform(
        "u_Model",
        outlineTransform);

    m_SelectionOutlineMaterial->Bind(Renderer::GetAPI());
    meshRenderer.Mesh->Draw();
}

} // namespace Raven
