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
    uint32_t width, uint32_t height, RHIBackend backend)
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
    outUI = {};
    if (viewportWidth == 0u || viewportHeight == 0u || defaultTexture == nullptr)
    {
        return false;
    }

    UITessellatedDrawList tessellated{};
    if (UITessellator::Tessellate(drawList, tessellated) == false)
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
    outUI.Commands = std::move(tessellated.Commands);
    outUI.Textures.reserve(outUI.Commands.size());
    for (const UITessellatedCommand& command : outUI.Commands)
    {
        outUI.Textures.push_back(
            command.UseTexture == true ?
            ResolveTexture(device, command.Texture, defaultTexture) :
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
            preparedUI.ViewportWidth, preparedUI.ViewportHeight,
            preparedUI.Backend)) == false)
    {
        return false;
    }

    for (std::size_t index = 0u; index < preparedUI.Commands.size(); ++index)
    {
        const UITessellatedCommand& command = preparedUI.Commands[index];
        if (command.IndexCount == 0u)
        {
            continue;
        }

        if (command.Clip.Enabled == true)
        {
            const float left = std::clamp(
                command.Clip.Rect.Min.x, 0.0f,
                static_cast<float>(preparedUI.ViewportWidth));
            const float top = std::clamp(
                command.Clip.Rect.Min.y, 0.0f,
                static_cast<float>(preparedUI.ViewportHeight));
            const float right = std::clamp(
                command.Clip.Rect.Max.x, 0.0f,
                static_cast<float>(preparedUI.ViewportWidth));
            const float bottom = std::clamp(
                command.Clip.Rect.Max.y, 0.0f,
                static_cast<float>(preparedUI.ViewportHeight));
            const uint32_t x = static_cast<uint32_t>(std::max(0.0f, left));
            const uint32_t y = static_cast<uint32_t>(std::max(0.0f, top));
            const uint32_t width = static_cast<uint32_t>(
                std::max(0.0f, right - left));
            const uint32_t height = static_cast<uint32_t>(
                std::max(0.0f, bottom - top));
            if (width == 0u || height == 0u)
            {
                continue;
            }
            if (commands.SetScissor(x, y, width, height) == false)
            {
                return false;
            }
        }
        else if (commands.SetScissor(
            0u, 0u, preparedUI.ViewportWidth,
            preparedUI.ViewportHeight) == false)
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
    const Ref<TextureAsset>& asset,
    const Ref<RHITexture>& defaultTexture)
{
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
