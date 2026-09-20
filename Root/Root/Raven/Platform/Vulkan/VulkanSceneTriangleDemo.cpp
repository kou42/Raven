#include "VulkanSceneTriangleDemo.h"

#include "Raven/Core/Window.h"
#include "Raven/Renderer/RHI/RHISceneDraw.h"

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

    // CPU上の頂点データをHost VisibleなScene Bufferへ転送します。
    // 色補間が確認できるよう各頂点に別のRGB値を設定します。
    const std::array<Vertex, 3> vertices = {{
        {{ 0.0f, -0.6f }, { 1.0f, 0.0f, 0.0f }},
        {{ 0.6f,  0.6f }, { 0.0f, 1.0f, 0.0f }},
        {{-0.6f,  0.6f }, { 0.0f, 0.0f, 1.0f }}
    }};
    const std::array<uint32_t, 3> indices = {{ 0, 1, 2 }};
    VulkanSceneResourceFactory vulkanResources(m_Context);
    RHISceneResourceFactory& resources = vulkanResources;
    m_VertexBuffer = resources.CreateVertexBuffer(vertices.data(),
        static_cast<uint32_t>(sizeof(vertices)), sizeof(Vertex));
    if (m_VertexBuffer == nullptr)
    {
        std::cerr << "Vulkan Scene Triangle: Vertex Buffer creation failed.\n";
        Shutdown();
        return false;
    }
    m_IndexBuffer = resources.CreateIndexBuffer(indices.data(),
        static_cast<uint32_t>(indices.size()));
    if (m_IndexBuffer == nullptr)
    {
        std::cerr << "Vulkan Scene Triangle: Index Buffer creation failed.\n";
        Shutdown();
        return false;
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
    // ColorFormatはFactoryが現在のScene RenderTargetから解決します。
    VulkanSceneResourceFactory vulkanResources(m_Context);
    RHISceneResourceFactory& resources = vulkanResources;
    m_Pipeline = resources.CreateGraphicsPipeline(specification);
    return m_Pipeline != nullptr;
}

RHIFrameResult VulkanSceneTriangleDemo::DrawFrame()
{
    if (m_Window == nullptr || m_Pipeline == nullptr ||
        m_VertexBuffer == nullptr || m_IndexBuffer == nullptr ||
        m_VertexBuffer->IsValid() == false || m_IndexBuffer->IsValid() == false)
    {
        return RHIFrameResult::FatalError;
    }

    // Frame境界は共通契約から呼び出し、Acquire/Submitの実装はContextに任せます。
    RHISceneFrameLifecycle& frame = m_Context;
    const RHIFrameResult begin = frame.BeginFrame();
    if (begin != RHIFrameResult::Success)
    {
        return begin;
    }

    VulkanSceneCommandList vulkanCommands(m_Context);
    // Scene描画側は共通CommandListだけを参照し、native記録はBackendへ委譲します。
    RHISceneCommandList& commands = vulkanCommands;
    const RHISceneDrawItem item(m_Pipeline, *m_VertexBuffer, *m_IndexBuffer);
    if (RHISceneDraw::Draw(commands, item) == false)
    {
        // BeginFrame成功後の記録失敗時はSubmit/PresentせずContextを破棄します。
        // Acquire済Semaphoreを再利用してはならないため、次Frameも禁止します。
        Shutdown();
        return RHIFrameResult::FatalError;
    }

    const RHIFrameResult end = frame.EndFrame();
    if (end != RHIFrameResult::Success)
    {
        Shutdown();
        return end;
    }
    const RHIFrameResult present = frame.Present();
    if (present == RHIFrameResult::FatalError)
    {
        Shutdown();
    }
    return present;
}

bool VulkanSceneTriangleDemo::Resize(uint32_t width, uint32_t height)
{
    if (m_Window == nullptr || width == 0 || height == 0)
    {
        return false;
    }

    RHISceneFrameLifecycle& frame = m_Context;
    if (frame.Resize(width, height) == false)
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
    m_IndexBuffer.reset();
    m_VertexBuffer.reset();
    m_Context.Shutdown();
    m_VertexShader = {};
    m_FragmentShader = {};
    m_Window = nullptr;
}
} // namespace Raven
