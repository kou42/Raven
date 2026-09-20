#include "VulkanSceneTriangleDemo.h"
#include "VulkanSceneRHIDevice.h"

#include "Raven/Core/Window.h"

#include <iostream>

namespace Raven
{
bool VulkanSceneTriangleDemo::Init(Window& window,
    const RHIShaderBinary& vertexShader, const RHIShaderBinary& fragmentShader)
{
    Shutdown();
    if (window.GetBackend() != RHIBackend::Vulkan ||
        vertexShader.Format != RHIShaderBinaryFormat::SPIRV ||
        fragmentShader.Format != RHIShaderBinaryFormat::SPIRV)
    {
        return false;
    }

    m_Window = &window;
    m_VertexShader = vertexShader;
    m_FragmentShader = fragmentShader;
    if (m_Context.Init(window) == false)
    {
        std::cerr << "Vulkan Scene Triangle: Scene Context initialization failed.\n";
        Shutdown();
        return false;
    }

    // 同じPipelineで2つの独立Meshを描画し、Frame内の複数Buffer追跡を検証します。
    // 左右に配置してDrawが片方だけ欠落した場合も視覚的に確認できます。
    const std::array<std::array<Vertex, 3>, MeshCount> meshVertices = {{
        {{
            {{-0.7f, -0.6f }, { 1.0f, 0.0f, 0.0f }},
            {{-0.1f, -0.6f }, { 0.0f, 1.0f, 0.0f }},
            {{-0.4f,  0.6f }, { 0.0f, 0.0f, 1.0f }}
        }},
        {{
            {{ 0.1f, -0.6f }, { 0.0f, 1.0f, 1.0f }},
            {{ 0.7f, -0.6f }, { 1.0f, 0.0f, 1.0f }},
            {{ 0.4f,  0.6f }, { 1.0f, 1.0f, 0.0f }}
        }}
    }};
    const std::array<uint32_t, 3> indices = {{ 0, 1, 2 }};
    VulkanSceneRHIDevice device(m_Context);
    for (std::size_t mesh = 0; mesh < MeshCount; ++mesh)
    {
        RHIBufferSpecification vertexSpecification{};
        vertexSpecification.Size = sizeof(meshVertices[mesh]);
        vertexSpecification.Usage = RHIBufferUsage::Vertex;
        vertexSpecification.DebugName = "Vulkan Scene Mesh Vertex";
        m_VertexBuffers[mesh] = device.CreateBuffer(
            vertexSpecification, meshVertices[mesh].data());
        if (m_VertexBuffers[mesh] == nullptr)
        {
            std::cerr << "Vulkan Scene Triangle: Mesh Vertex Buffer creation failed.\n";
            Shutdown();
            return false;
        }

        RHIBufferSpecification indexSpecification{};
        indexSpecification.Size = sizeof(indices);
        indexSpecification.Usage = RHIBufferUsage::Index;
        indexSpecification.DebugName = "Vulkan Scene Mesh Index";
        m_IndexBuffers[mesh] = device.CreateBuffer(
            indexSpecification, indices.data());
        if (m_IndexBuffers[mesh] == nullptr)
        {
            std::cerr << "Vulkan Scene Triangle: Mesh Index Buffer creation failed.\n";
            Shutdown();
            return false;
        }
    }
    if (CreatePipeline() == false)
    {
        std::cerr << "Vulkan Scene Triangle: Graphics Pipeline creation failed.\n";
        Shutdown();
        return false;
    }
    return true;
}

bool VulkanSceneTriangleDemo::CreatePipeline()
{
    RHIGraphicsPipelineSpecification specification;
    specification.VertexShader = m_VertexShader;
    specification.FragmentShader = m_FragmentShader;
    specification.VertexBindings = {{ 0, sizeof(Vertex) }};
    specification.VertexAttributes = {
        { 0, 0, ShaderDataType::Float2, 0 },
        { 1, 0, ShaderDataType::Float3, sizeof(Vertex::Position) }
    };
    specification.Cull = CullMode::None;
    specification.DebugName = "Vulkan Scene Triangle";
    switch (m_Context.GetColorFormat())
    {
    case VK_FORMAT_R8G8B8A8_UNORM:
        specification.ColorFormat = RHIColorFormat::RGBA8Unorm;
        break;
    case VK_FORMAT_B8G8R8A8_UNORM:
        specification.ColorFormat = RHIColorFormat::BGRA8Unorm;
        break;
    case VK_FORMAT_R8G8B8A8_SRGB:
        specification.ColorFormat = RHIColorFormat::RGBA8Srgb;
        break;
    case VK_FORMAT_B8G8R8A8_SRGB:
        specification.ColorFormat = RHIColorFormat::BGRA8Srgb;
        break;
    default:
        // SwapChainの実FormatとPipelineのAttachment Formatを一致させます。
        std::cerr << "Vulkan Scene Triangle: Unsupported SwapChain color format: "
            << static_cast<int>(m_Context.GetColorFormat()) << '\n';
        return false;
    }
    m_Pipeline = m_Context.CreateGraphicsPipeline(specification);
    return m_Pipeline != nullptr;
}

RHIFrameResult VulkanSceneTriangleDemo::DrawFrame()
{
    if (m_Window == nullptr || m_Pipeline == nullptr)
    {
        return RHIFrameResult::FatalError;
    }
    for (std::size_t mesh = 0; mesh < MeshCount; ++mesh)
    {
        if (m_VertexBuffers[mesh] == nullptr || m_IndexBuffers[mesh] == nullptr)
        {
            return RHIFrameResult::FatalError;
        }
    }

    const RHIFrameResult begin = m_Context.BeginFrame();
    if (begin != RHIFrameResult::Success)
    {
        return begin;
    }

    VulkanSceneCommandList commands(m_Context);
    if (commands.BindPipeline(m_Pipeline) == false)
    {
        Shutdown();
        return RHIFrameResult::FatalError;
    }
    for (std::size_t mesh = 0; mesh < MeshCount; ++mesh)
    {
        // 同じFrameに異なるVertex/Index Bufferを記録します。
        // BeginFrame成功後の失敗時はAcquire済Semaphoreを再利用せず破棄します。
        if (commands.DrawIndexed(m_VertexBuffers[mesh], m_IndexBuffers[mesh]) == false)
        {
            Shutdown();
            return RHIFrameResult::FatalError;
        }
    }

    const RHIFrameResult end = m_Context.EndFrame();
    if (end != RHIFrameResult::Success)
    {
        Shutdown();
        return end;
    }
    const RHIFrameResult present = m_Context.Present();
    if (present == RHIFrameResult::FatalError)
    {
        Shutdown();
    }
    return present;
}

bool VulkanSceneTriangleDemo::Resize(uint32_t width, uint32_t height)
{
    if (m_Window == nullptr || width == 0 || height == 0 ||
        m_Context.Resize(width, height) == false)
    {
        return false;
    }
    // Resize時にScene Contextが旧RenderPass用Pipelineを無効化します。
    // 新SwapChainのFormatを読み直してから再生成します。
    m_Pipeline.reset();
    return CreatePipeline();
}

void VulkanSceneTriangleDemo::Shutdown()
{
    // BufferはGPUが参照しているため、必ずWaitIdle後、Device破棄前に解放します。
    if (m_Context.GetDevice().IsValid() == true)
    {
        m_Context.GetDevice().WaitIdle();
    }
    m_Pipeline.reset();
    // native VkBufferを所有するRefはContextのVkDeviceより先に破棄します。
    for (std::size_t mesh = 0; mesh < MeshCount; ++mesh)
    {
        m_IndexBuffers[mesh].reset();
        m_VertexBuffers[mesh].reset();
    }
    m_Context.Shutdown();
    m_VertexShader = {};
    m_FragmentShader = {};
    m_Window = nullptr;
}
} // namespace Raven
