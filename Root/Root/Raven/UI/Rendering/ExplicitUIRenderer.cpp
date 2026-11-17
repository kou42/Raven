#include "Raven/UI/Rendering/ExplicitUIRenderer.h"

#include "Raven/Assets/TextureAsset.h"
#include "Raven/Renderer/RHI/RHIDevice.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace Raven
{
namespace
{
std::array<float, 16> BuildUIClipTransform(
    float width, float height, RHIBackend backend)
{
    const float sx = 2.0f / static_cast<float>(width);
    // Vulkanは正のViewport heightを使うためNDC -YがFramebuffer上端です。
    // DX12はNDC +Yが上端なので、pixel Yの係数とoffsetをBackendごとに切り替えます。
    const bool vulkan = backend == RHIBackend::Vulkan;
    const float sy = (vulkan == true ? 2.0f : -2.0f) /
        static_cast<float>(height);
    const float ty = vulkan == true ? -1.0f : 1.0f;
    return {
        sx, 0.0f, 0.0f, 0.0f,
        0.0f, sy, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, ty, 0.0f, 1.0f
    };
}
} // namespace

bool ExplicitUIRenderer::CreatePipeline(
    RHIDevice& device,
    const RHIShaderBinary& vertexShader,
    const RHIShaderBinary& fragmentShader,
    Ref<RHIGraphicsPipeline>& outPipeline)
{
    outPipeline.reset();
    RHIGraphicsPipelineTarget target{};
    if (device.GetGraphicsPipelineTarget(target) == false ||
        target.IsValid() == false)
    {
        return false;
    }

    RHIGraphicsPipelineSpecification specification{};
    specification.VertexShader = vertexShader;
    specification.FragmentShader = fragmentShader;
    specification.VertexBindings.push_back({ 0u, sizeof(UIVertex) });
    specification.VertexAttributes.push_back(
        { 0u, 0u, ShaderDataType::Float2, offsetof(UIVertex, Position) });
    specification.VertexAttributes.push_back(
        { 1u, 0u, ShaderDataType::Float4, offsetof(UIVertex, Color) });
    specification.VertexAttributes.push_back(
        { 2u, 0u, ShaderDataType::Float2, offsetof(UIVertex, Texcoord) });
    specification.Topology = PrimitiveTopology::Triangles;
    specification.Cull = CullMode::None;
    specification.DepthTest = false;
    specification.DepthWrite = false;
    specification.Blend = true;
    // Legacy OpenGL UIのglBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)とAlpha出力まで一致させます。
    // Scene Pipelineの既定SourceOver合成は変更しません。
    specification.AlphaBlend = RHIAlphaBlendMode::SourceAlphaWeighted;
    specification.ColorFormat = target.ColorFormat;
    specification.DepthFormat = target.DepthFormat;
    specification.SampleCount = target.SampleCount;
    specification.DebugName = "Explicit Raven UI Pipeline";

    outPipeline = device.CreateGraphicsPipeline(specification);
    return outPipeline != nullptr;
}

bool ExplicitUIRenderer::Prepare(
    RHIDevice& device,
    const UIDrawList& drawList,
    uint32_t viewportWidth,
    uint32_t viewportHeight,
    const Ref<RHITexture>& defaultTexture,
    const Ref<RHIGraphicsPipeline>& pipeline,
    PreparedExplicitUI& outUI)
{
    return Prepare(device, drawList, math::Vec2(static_cast<float>(viewportWidth), static_cast<float>(viewportHeight)),
        viewportWidth, viewportHeight, defaultTexture, pipeline, outUI);
}

bool ExplicitUIRenderer::Prepare(RHIDevice& device, const UIDrawList& drawList,
    const math::Vec2& logicalSize, uint32_t viewportWidth, uint32_t viewportHeight,
    const Ref<RHITexture>& defaultTexture, const Ref<RHIGraphicsPipeline>& pipeline, PreparedExplicitUI& outUI)
{
    outUI = {};
    if (viewportWidth == 0u || viewportHeight == 0u || defaultTexture == nullptr)
    {
        return false;
    }

    UIRenderGeometry tessellated{};
    if (UIGeometryBuilder::Build(drawList, logicalSize, viewportWidth, viewportHeight, tessellated) == false)
    {
        return false;
    }
    outUI.LogicalSize = logicalSize;
    if (tessellated.Batches.empty() == true)
    {
        // UIが空のFrameは失敗ではありません。
        outUI.ViewportWidth = viewportWidth;
        outUI.ViewportHeight = viewportHeight;
        outUI.Backend = device.GetBackend();
        return true;
    }

    RHIBufferSpecification vertexSpecification{};
    vertexSpecification.Usage = RHIBufferUsage::Vertex;
    vertexSpecification.Size = tessellated.Vertices.size() * sizeof(UIVertex);
    vertexSpecification.DebugName = "Raven UI Vertex Buffer";

    RHIBufferSpecification indexSpecification{};
    indexSpecification.Usage = RHIBufferUsage::Index;
    indexSpecification.Size = tessellated.Indices.size() * sizeof(uint32_t);
    indexSpecification.DebugName = "Raven UI Index Buffer";

    Ref<RHIBuffer> vertexBuffer = device.CreateBuffer(
        vertexSpecification, tessellated.Vertices.data());
    Ref<RHIBuffer> indexBuffer = device.CreateBuffer(
        indexSpecification, tessellated.Indices.data());
    if (vertexBuffer == nullptr || indexBuffer == nullptr)
    {
        return false;
    }

    outUI.VertexBuffer = std::move(vertexBuffer);
    outUI.IndexBuffer = std::move(indexBuffer);
    outUI.Commands = std::move(tessellated.Batches);
    outUI.Textures.reserve(outUI.Commands.size());
    for (const UIRenderBatch& command : outUI.Commands)
    {
        outUI.Textures.push_back(
            command.UseTexture == true ?
            ResolveTexture(device, command.TextureView, defaultTexture) :
            defaultTexture);
        if (outUI.Textures.back() == nullptr)
        {
            return false;
        }
    }
    outUI.ViewportWidth = viewportWidth;
    outUI.ViewportHeight = viewportHeight;
    outUI.Backend = device.GetBackend();
    if (device.PrepareSceneTextures(outUI.Textures, pipeline) == false)
    {
        return false;
    }
    return true;
}

bool ExplicitUIRenderer::Draw(
    RHISceneCommandList& commands,
    const Ref<RHIGraphicsPipeline>& pipeline,
    const PreparedExplicitUI& preparedUI) const
{
    if (pipeline == nullptr || preparedUI.ViewportWidth == 0u ||
        preparedUI.ViewportHeight == 0u)
    {
        return false;
    }
    if (preparedUI.Commands.empty())
    {
        return true;
    }
    if (preparedUI.VertexBuffer == nullptr || preparedUI.IndexBuffer == nullptr ||
        preparedUI.Textures.size() != preparedUI.Commands.size() ||
        commands.BindPipeline(pipeline) == false ||
        commands.SetViewport(0u, 0u, preparedUI.ViewportWidth,
            preparedUI.ViewportHeight) == false ||
        commands.SetClipTransform(BuildUIClipTransform(
            preparedUI.LogicalSize.x, preparedUI.LogicalSize.y,
            preparedUI.Backend)) == false)
    {
        return false;
    }

    for (std::size_t index = 0u; index < preparedUI.Commands.size(); ++index)
    {
        const UIRenderBatch& command = preparedUI.Commands[index];
        if (command.IndexCount == 0u)
        {
            continue;
        }

        // BuilderのScissorは左上原点の実Pixel。DX12/VulkanのCommand契約と一致します。
        if (commands.SetScissor(command.Clip.X, command.Clip.Y,
            command.Clip.Width, command.Clip.Height) == false)
        {
            return false;
        }

        RHIMaterialProperties material{};
        material.Texture = preparedUI.Textures[index];
        material.SurfaceType = MaterialSurfaceType::Transparent;
        if (commands.BindMaterial(material) == false ||
            commands.DrawIndexed(preparedUI.VertexBuffer,
                preparedUI.IndexBuffer, command.IndexCount,
                command.FirstIndex) == false)
        {
            return false;
        }
    }
    return true;
}

Ref<RHITexture> ExplicitUIRenderer::ResolveTexture(
    RHIDevice& device,
    const UITextureView& textureView,
    const Ref<RHITexture>& defaultTexture)
{
    if (textureView.GetRHITexture() != nullptr)
    {
        return textureView.GetRHITexture();
    }

    const Ref<TextureAsset>& asset = textureView.GetAsset();
    if (asset == nullptr || asset->HasPixelData() == false)
    {
        return defaultTexture;
    }

    const auto found = m_TextureCache.find(asset.get());
    if (found != m_TextureCache.end())
    {
        return found->second;
    }

    Ref<RHITexture> texture = asset->CreateRHITexture(device);
    if (texture == nullptr)
    {
        return nullptr;
    }
    m_TextureCache.emplace(asset.get(), texture);
    m_CachedAssets.push_back(asset);
    return texture;
}

void ExplicitUIRenderer::ClearTextureCache()
{
    m_TextureCache.clear();
    m_CachedAssets.clear();
}
} // namespace Raven
