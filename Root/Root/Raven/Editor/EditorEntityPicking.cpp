#include "Raven/Editor/EditorLayer.h"

#include "Raven/Core/Application.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <cassert>
#include <limits>

namespace Raven
{

void EditorLayer::RenderEntityPickingPass(
    Framebuffer& framebuffer,
    const Camera& camera)
{
    if (m_Application == nullptr)
    {
        return;
    }

    Scene* activeScene = m_Application->GetScene();
    if (activeScene == nullptr)
    {
        return;
    }

    // Scene View用Framebufferは
    //   Color 0 = RGBA8 Scene表示
    //   Color 1 = R32I Entity Picking
    //   Depth   = Depth24Stencil8
    // という構成で生成します。
    // Game View等の従来Framebufferへ誤ってPicking Passを適用しないよう、Attachment数とFormatを検証します。
    if (framebuffer.GetColorAttachmentCount() <= 1)
    {
        return;
    }

    const Ref<Texture>& pickingAttachment = framebuffer.GetColorAttachment(1);
    if (pickingAttachment == nullptr
        || pickingAttachment->GetSpecification().Format != TextureFormat::R32I)
    {
        return;
    }

    // ========================================================================
    // Lazy creation of Editor-only picking material
    // ========================================================================
    // Picking Shaderはゲーム内Materialとは完全に分離します。
    // そのため通常ShaderへEditor専用のEntity ID出力を追加する必要がなく、ゲーム描画APIを汚しません。
    if (m_EntityPickingMaterial == nullptr)
    {
        Ref<Shader> pickingShader = Shader::Create(
            "Raven/Assets/Shaders/Glsl/EntityPicking.glsl");

        if (pickingShader == nullptr)
        {
            assert(false && "Failed to create Entity Picking shader.");
            return;
        }

        PipelineSpecification pipelineSpecification{};
        pipelineSpecification.DebugName = "Editor Entity Picking Pipeline";
        pipelineSpecification.Shader = pickingShader;
        pipelineSpecification.Topology = PrimitiveTopology::Triangles;

        // 通常SceneにはCull=NoneのMaterialも存在するため、PickingだけBack Faceを落として
        // 見えているGeometryを選択不能にしないよう両面を描画します。
        pipelineSpecification.Cull = CullMode::None;
        pipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;

        // 通常Scene描画済みのDepth Bufferを再利用します。
        // 同じGeometryを再描画するとDepth値はほぼ等しいためLessではなくLessEqualを使い、
        // Picking Pass自身はDepthを書き換えずR32I Attachmentだけを更新します。
        pipelineSpecification.DepthTest = true;
        pipelineSpecification.DepthWrite = false;
        pipelineSpecification.DepthCompare = DepthCompareOperator::LessEqual;
        pipelineSpecification.Blend = false;

        Ref<Pipeline> pickingPipeline = Pipeline::Create(pipelineSpecification);
        if (pickingPipeline == nullptr)
        {
            assert(false && "Failed to create Entity Picking pipeline.");
            return;
        }

        m_EntityPickingMaterial = CreateRef<Material>(pickingPipeline);
    }

    // Entityが存在しないpixelは-1にします。
    // 0はRavenのInvalidEntityIndexですが、-1を使うことでClear済みpixelとEntity Indexの
    // 値域を視覚的にも明確に分離できます。
    framebuffer.ClearAttachment(1, -1);

    // Camera UniformはPass全体で共通です。
    // Renderer::BeginScene()/EndScene()を使うとDebug Overlayまで再描画されるため、
    // Picking PassではMaterialへ直接設定し、Mesh::Draw()だけを発行します。
    m_EntityPickingMaterial->SetUniform("u_View", camera.GetViewMatrix());
    m_EntityPickingMaterial->SetUniform("u_Projection", camera.GetProjectionMatrix());

    // ========================================================================
    // ECS Entity ID rendering
    // ========================================================================
    // SceneGame固有の所有ListではなくECS Viewを走査することで、Cloth / Jelly / Human等、
    // Layer側が生成したMeshRenderer Entityも同じPicking経路へ自動的に参加します。
    for (auto [entity, transform, meshRenderer]
        : activeScene->View<TransformComponent, MeshRendererComponent>())
    {
        if (meshRenderer.IsValid() == false)
        {
            continue;
        }

        if (entity.GetIndex()
            > static_cast<EntityIndex>(std::numeric_limits<int>::max()))
        {
            assert(false && "EntityIndex exceeds R32I picking range.");
            continue;
        }

        m_EntityPickingMaterial->SetUniform(
            "u_EntityID",
            static_cast<int>(entity.GetIndex()));
        m_EntityPickingMaterial->SetUniform(
            "u_Model",
            transform.GetTransform());

        m_EntityPickingMaterial->Bind(Renderer::GetAPI());
        meshRenderer.Mesh->Draw();
    }
}

} // namespace Raven
