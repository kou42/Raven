#include "VulkanSceneRuntime.h"

#include "VulkanSceneCommandList.h"
#include "VulkanSceneRHIDevice.h"

#include "Raven/Core/Window.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/RHI/RHISceneDrawItemBuilder.h"

#include <array>
#include <utility>

namespace Raven
{

VulkanSceneRuntime::~VulkanSceneRuntime()
{
    Shutdown();
}

bool VulkanSceneRuntime::Init(
    Window& window,
    const Ref<Material>& pipelineMaterial,
    const RHIShaderAssetSpecification& vertexShader,
    const RHIShaderAssetSpecification& fragmentShader)
{
    Shutdown();
    if (window.GetBackend() != RHIBackend::Vulkan ||
        pipelineMaterial == nullptr ||
        pipelineMaterial->GetPipeline() == nullptr)
    {
        return false;
    }

    m_Window = &window;
    if (m_Context.Init(window) == false)
    {
        Shutdown();
        return false;
    }

    m_Device = CreateScope<VulkanSceneRHIDevice>(m_Context);
    m_PipelineMaterial = pipelineMaterial;
    m_VertexShader = m_ShaderAssets.Load(vertexShader, RHIBackend::Vulkan);
    m_FragmentShader = m_ShaderAssets.Load(fragmentShader, RHIBackend::Vulkan);
    if (m_VertexShader == nullptr || m_FragmentShader == nullptr ||
        CreatePipelines() == false ||
        CreateDefaultTexture() == false)
    {
        Shutdown();
        return false;
    }

    m_Initialized = true;
    return true;
}

bool VulkanSceneRuntime::PrepareMesh(const Ref<Mesh>& mesh)
{
    if (m_Initialized == false || m_Device == nullptr || mesh == nullptr ||
        m_Context.GetActiveCommandBuffer() != VK_NULL_HANDLE)
    {
        return false;
    }

    // Meshが別ContextのBufferを保持している可能性があるため、このDeviceで必ず再構築します。
    return mesh->BuildRHIResources(*m_Device);
}

RHIFrameResult VulkanSceneRuntime::DrawFrame()
{
    if (m_Initialized == false || m_Device == nullptr ||
        m_OpaquePipeline == nullptr || m_TransparentPipeline == nullptr ||
        m_DefaultTexture == nullptr)
    {
        return RHIFrameResult::FatalError;
    }

    VulkanSceneCommandList commands(m_Context);
    const RHIFrameResult result = Renderer::DrawRHISceneFrame(
        *m_Device,
        m_Context,
        commands,
        m_OpaquePipeline,
        m_TransparentPipeline,
        m_DefaultTexture,
        RHISceneDrawItemBuilder::VulkanClipCorrection());
    if (result == RHIFrameResult::FatalError)
    {
        // Acquire後の記録失敗を含むFatal状態ではContextを再利用しません。
        Shutdown();
    }
    return result;
}

bool VulkanSceneRuntime::Resize(uint32_t width, uint32_t height)
{
    if (m_Initialized == false || width == 0 || height == 0)
    {
        return false;
    }

    if (m_Context.Resize(width, height) == false)
    {
        return false;
    }

    // Context::Resize()が旧RenderPass用native Pipelineを無効化しています。
    // 外部Refも破棄し、新しいRender Target情報から両Pipelineを再生成します。
    m_OpaquePipeline.reset();
    m_TransparentPipeline.reset();
    if (CreatePipelines() == false)
    {
        Shutdown();
        return false;
    }
    return true;
}

void VulkanSceneRuntime::Shutdown()
{
    if (m_Context.GetDevice().IsValid() == true)
    {
        m_Context.GetDevice().WaitIdle();
    }

    // Contextより先に上位Resource参照を解放します。
    // Context自身も外部Refが残ったResourceをDevice破棄前に無効化します。
    m_OpaquePipeline.reset();
    m_TransparentPipeline.reset();
    m_DefaultTexture.reset();
    m_PipelineMaterial.reset();
    m_VertexShader.reset();
    m_FragmentShader.reset();
    m_ShaderAssets.Clear();
    m_Device.reset();
    m_Context.Shutdown();
    m_Window = nullptr;
    m_Initialized = false;
}

bool VulkanSceneRuntime::IsInitialized() const
{
    return m_Initialized;
}

RHIDevice* VulkanSceneRuntime::GetDevice()
{
    return m_Initialized == true ? m_Device.get() : nullptr;
}

const Ref<RHITexture>& VulkanSceneRuntime::GetDefaultTexture() const
{
    return m_DefaultTexture;
}

uint32_t VulkanSceneRuntime::GetWidth() const
{
    return m_Context.GetExtent().width;
}

uint32_t VulkanSceneRuntime::GetHeight() const
{
    return m_Context.GetExtent().height;
}

bool VulkanSceneRuntime::CreatePipelines()
{
    if (m_Device == nullptr || m_PipelineMaterial == nullptr ||
        m_VertexShader == nullptr || m_FragmentShader == nullptr ||
        m_VertexShader->IsValid() == false ||
        m_FragmentShader->IsValid() == false)
    {
        return false;
    }

    Ref<RHIGraphicsPipeline> opaque;
    Ref<RHIGraphicsPipeline> transparent;
    if (Renderer::CreateRHIScenePipelines(
        *m_Device,
        *m_PipelineMaterial,
        m_VertexShader->GetBinary(),
        m_FragmentShader->GetBinary(),
        opaque,
        transparent) == false)
    {
        return false;
    }

    // 両方揃ってから差し替え、Resize途中で片方だけを公開しません。
    m_OpaquePipeline = std::move(opaque);
    m_TransparentPipeline = std::move(transparent);
    return true;
}

bool VulkanSceneRuntime::CreateDefaultTexture()
{
    if (m_Device == nullptr)
    {
        return false;
    }

    RHITextureSpecification specification{};
    specification.Width = 1;
    specification.Height = 1;
    specification.Format = RHITextureFormat::RGBA8;
    specification.Usage = RHITextureUsage::Sampled;
    specification.GenerateMips = false;
    specification.DebugName = "Vulkan Scene Default White";

    constexpr std::array<uint8_t, 4> white = {255u, 255u, 255u, 255u};
    Ref<RHITexture> texture = m_Device->CreateTexture(
        specification,
        white.data(),
        white.size());
    if (texture == nullptr)
    {
        return false;
    }

    m_DefaultTexture = std::move(texture);
    return true;
}

} // namespace Raven
