#include "Raven/UI/Rendering/OpenGLUIRenderer.h"

#include "Raven/Assets/TextureAsset.h"
#include "Raven/Renderer/Buffer/BufferLayout.h"
#include "Raven/Renderer/Buffer/IndexBuffer.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Buffer/VertexBuffer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/UI/Core/UIDrawList.h"
#include "Raven/UI/Rendering/UIGeometryBuilder.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <limits>
#include <vector>

namespace Raven
{



OpenGLUIRenderer::OpenGLUIRenderer()
{
    m_VertexArray = VertexArray::Create();
    m_Shader = Shader::Create("Raven/Assets/Shaders/Glsl/UI.glsl");
    if (m_Shader != nullptr)
    {
        PipelineSpecification specification{};
        specification.Shader = m_Shader;
        specification.Topology = PrimitiveTopology::Triangles;
        specification.Cull = CullMode::None;
        specification.DepthTest = false;
        specification.DepthWrite = false;
        specification.Blend = true;
        specification.DebugName = "Raven UI Overlay";
        m_Pipeline = Pipeline::Create(specification);
    }
}

void OpenGLUIRenderer::Render(
    const UIDrawList& drawList,
    const math::Vec2& viewportSize,
    const math::Vec2& framebufferSize)
{
    if (std::isfinite(framebufferSize.x) == false || std::isfinite(framebufferSize.y) == false ||
        static_cast<double>(framebufferSize.x) > (std::numeric_limits<int32_t>::max)() ||
        static_cast<double>(framebufferSize.y) > (std::numeric_limits<int32_t>::max)() ||
        viewportSize.x <= 0.0f || viewportSize.y <= 0.0f ||
        framebufferSize.x <= 0.0f || framebufferSize.y <= 0.0f)
    {
        return;
    }

    if (drawList.IsEmpty())
    {
        return;
    }

    if (m_VertexArray == nullptr || m_Shader == nullptr || m_Pipeline == nullptr)
    {
#ifdef _DEBUG
        static bool missingResourceLogged = false;
        if (missingResourceLogged == false)
        {
            std::cout
                << "[Raven UI] Render resource missing. VAO="
                << (m_VertexArray != nullptr ? "valid" : "null")
                << ", Shader="
                << (m_Shader != nullptr ? "valid" : "null")
                << '\n';
            missingResourceLogged = true;
        }
#endif
        return;
    }

    // Shape tessellationはBackend共通層で完了させ、OpenGLはGPU uploadとstate管理だけを担当します。
    UIRenderGeometry tessellated{};
    if (UIGeometryBuilder::Build(drawList, viewportSize,
        static_cast<uint32_t>(framebufferSize.x), static_cast<uint32_t>(framebufferSize.y), tessellated) == false ||
        tessellated.Batches.empty() == true)
    {
        return;
    }

    // VAO/VBOは初回EnsureBuffersで変更されるため、GPU Buffer更新前に退避します。
    // ShaderとTexture Unit 0も同じsnapshotで保持し、失敗経路でも復元できます。
    const RHIOverlayBindingState previousBinding = RenderCommand::CaptureOverlayBindingState();

    EnsureBuffers(
        reinterpret_cast<const float*>(tessellated.Vertices.data()),
        static_cast<uint32_t>(tessellated.Vertices.size() * sizeof(UIVertex)),
        tessellated.Indices.data(),
        static_cast<uint32_t>(tessellated.Indices.size()));

    if (m_VertexBuffer == nullptr || m_IndexBuffer == nullptr)
    {
        // 初回Buffer作成に失敗した場合も、作成途中で変更したbindingを残しません。
        RenderCommand::RestoreOverlayBindingState(previousBinding);
#ifdef _DEBUG
        static bool missingBufferLogged = false;
        if (missingBufferLogged == false)
        {
            std::cout
                << "[Raven UI] Dynamic buffer creation failed. VBO="
                << (m_VertexBuffer != nullptr ? "valid" : "null")
                << ", EBO="
                << (m_IndexBuffer != nullptr ? "valid" : "null")
                << '\n';
            missingBufferLogged = true;
        }
#endif
        return;
    }

    // ========================================================================
    // UI render target / OpenGL state
    // ========================================================================
    // EditorはScene View / Game ViewをFramebufferへ描画し、そのTextureをUI内に表示します。
    // 直前の描画先がoff-screenの場合でも、UIは対象WindowのFramebufferへ描く必要があります。
    //
    // Main Window用UIContextは最終Window Overlayを担当するため、ここではdefault framebuffer(0)と
    // Window全体のviewportを明示的に選択します。将来Game View / RenderTexture用UIContextを追加する際は、
    // UIContext側へRenderTargetを持たせ、この固定0をContext指定のFramebufferへ置き換えます。
    //
    // さらに、直前の3D PipelineがPolygonMode / ColorMask / DepthMaskなどを変更していても
    // UI描画結果が影響を受けないよう、UI backendが必要なstateを明示し、描画後にすべて復元します。
    // Image描画ではTexture Unit 0も変更するため、Active TextureとBindingも同じ方針で保存・復元します。
    const Ref<Pipeline> previousPipeline = RenderCommand::GetBoundPipeline();
    const RHIRenderTargetState previousRenderTarget = RenderCommand::CaptureRenderTargetState();
    const RHIViewport previousViewport = RenderCommand::GetViewport();
    const RHIScissor previousScissor = RenderCommand::GetScissor();
    // 固定機能stateのsnapshotはRHI Backendが所有し、UIはOpenGL enumを解釈しません。
    const RHIOverlayRasterState previousRasterState = RenderCommand::CaptureOverlayRasterState();
    RenderCommand::BindDefaultRenderTarget();

    // Overlayの描画先をdefault framebufferへ切り替えた後、RHI経由でviewportを設定します。
    RenderCommand::SetViewport(0u, 0u,
        static_cast<uint32_t>(framebufferSize.x),
        static_cast<uint32_t>(framebufferSize.y));

    RenderCommand::SetScissor(false, 0u, 0u, 0u, 0u);
    RenderCommand::SetOverlayRasterState();

    // UI専用Triangle PipelineをBindし、直前のScene Line/Point topologyを引き継ぎません。
    RenderCommand::BindPipeline(m_Pipeline);
    RenderCommand::UploadUniform("u_ViewportSize", viewportSize);
    RenderCommand::UploadUniform("u_Texture", 0);
    m_VertexArray->Bind();

    // UI専用Triangle PipelineをRenderCommandへbind済みなので、
    // 直前SceneのLine/Point topologyを継承せず、RHIのDrawIndexedを利用します。
    // ImageではUITextureViewからTextureへの解決もBackend内で完了し、Widgetへnative IDを公開しません。
    // Polygonを含むIndex数の差は共通BuilderのFirstIndex/IndexCountで保持します。
    // 共通Builderが確定したFramebuffer Pixel Clip/Batchを使用します。
    // 論理座標からPixelへの丸めはここでは繰り返さず、GL固有のY反転だけを担当します。
    for (const UIRenderBatch& batch : tessellated.Batches)
    {
        RenderCommand::SetScissor(true, batch.Clip.X,
            static_cast<int32_t>(framebufferSize.y) - static_cast<int32_t>(batch.Clip.Y + batch.Clip.Height),
            batch.Clip.Width, batch.Clip.Height);
        bool useTexture = false;
        if (batch.UseTexture == true)
        {
            Ref<Texture> texture = batch.TextureView.GetLegacyTexture();
            if (texture == nullptr && batch.TextureView.GetAsset() != nullptr)
            {
                texture = batch.TextureView.GetAsset()->GetTexture();
            }
            if (texture != nullptr)
            {
                texture->Bind(0);
                useTexture = true;
            }
        }

        RenderCommand::UploadUniform("u_UseTexture", useTexture ? 1 : 0);

        if (batch.IndexCount > 0u)
        {
            // 共通Builderが保持する連続Index範囲をそのままLegacy RHIへ渡します。
            RenderCommand::DrawIndexed(
                m_VertexArray, batch.IndexCount, batch.FirstIndex);
        }
    }

    // 以前は初回描画の切り分けとしてglReadPixels()でBack Bufferを読み戻していました。
    // 描画経路が正常であることを確認できたため、通常実行時にGPU同期を発生させないようReadback診断は終了しています。

    // UI Pipeline追跡を元へ戻し、native bindingはBackendのsnapshotから復元します。
    RenderCommand::RestorePipelineBinding(previousPipeline);
    RenderCommand::RestoreOverlayBindingState(previousBinding);

    // ========================================================================
    // State restore
    // ========================================================================
    // Raven UIをRenderer pipelineの途中から呼んでも後続描画へ影響を残さないよう、
    // RenderTarget / Viewport / Scissorと固定機能stateも呼び出し前の値へ戻します。
    RenderCommand::RestoreRenderTargetState(previousRenderTarget);
    // UIの描画前に取得したViewportをRHI経由で復元します。
    RenderCommand::SetViewport(
        previousViewport.X, previousViewport.Y,
        previousViewport.Width, previousViewport.Height);
    // 無効時も以前のScissor Boxを復元し、次の描画passが同じstateから開始できるようにします。
    RenderCommand::SetScissor(true,
        previousScissor.X, previousScissor.Y,
        previousScissor.Width, previousScissor.Height);
    if (previousScissor.Enabled == false)
    {
        RenderCommand::SetScissor(false, 0u, 0u, 0u, 0u);
    }
    RenderCommand::RestoreOverlayRasterState(previousRasterState);

}

void OpenGLUIRenderer::EnsureBuffers(
    const float* vertices,
    uint32_t vertexDataSize,
    const uint32_t* indices,
    uint32_t indexCount)
{
    if (vertexDataSize == 0 || indexCount == 0)
    {
        return;
    }

    if (m_VertexBuffer == nullptr)
    {
        m_VertexBuffer = VertexBuffer::Create(vertices, vertexDataSize);
        if (m_VertexBuffer == nullptr)
        {
            return;
        }

        m_VertexBuffer->SetLayout({
            { ShaderDataType::Float2, "a_Position" },
            { ShaderDataType::Float4, "a_Color" },
            { ShaderDataType::Float2, "a_TexCoord" }
        });

        m_VertexArray->AddVertexBuffer(m_VertexBuffer);
    }
    else
    {
        m_VertexBuffer->SetData(vertices, vertexDataSize);
    }

    if (m_IndexBuffer == nullptr)
    {
        m_IndexBuffer = IndexBuffer::Create(indices, indexCount);
        if (m_IndexBuffer == nullptr)
        {
            return;
        }

        m_VertexArray->SetIndexBuffer(m_IndexBuffer);
    }
    else
    {
        m_IndexBuffer->SetData(indices, indexCount);
    }
}

} // namespace Raven
