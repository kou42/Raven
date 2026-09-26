#include "VulkanSceneRuntime.h"

#include "VulkanSceneCommandList.h"
#include "VulkanSceneRHIDevice.h"

#include "Raven/Core/Window.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/RHI/RHISceneDrawItemBuilder.h"
#include "Raven/Scene/Scene.h"

#include <array>
#include <utility>

namespace Raven
{

// VulkanSceneRHIDeviceの完全型が見える実装側で特殊メンバーを定義し、
// header利用側がunique_ptrの破棄処理を生成しないようにします。
VulkanSceneRuntime::VulkanSceneRuntime() = default;

VulkanSceneRuntime::~VulkanSceneRuntime()
{
    Shutdown();
}

bool VulkanSceneRuntime::Init(
    Window& window,
    const PipelineSpecification& pipelineSpecification,
    const RHIShaderAssetSpecification& vertexShader,
    const RHIShaderAssetSpecification& fragmentShader)
{
    Shutdown();
    if (window.GetBackend() != RHIBackend::Vulkan ||
        pipelineSpecification.Topology == PrimitiveTopology::None)
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
    m_PipelineDebugName = pipelineSpecification.DebugName != nullptr ?
        pipelineSpecification.DebugName : "Unnamed Pipeline";
    m_PipelineSpecification = pipelineSpecification;
    m_PipelineSpecification.DebugName = m_PipelineDebugName.c_str();
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

bool VulkanSceneRuntime::PrepareScene(Scene& scene)
{
    if (m_Initialized == false || m_Device == nullptr ||
        m_Context.GetActiveCommandBuffer() != VK_NULL_HANDLE)
    {
        return false;
    }

    // Deviceの所有者はRuntimeです。SceneはBackendに依存せず、
    // 同じMeshを参照するEntityのGPU BufferをFrame前にまとめて構築します。
    return scene.PrepareRHIMeshes(*m_Device);
}

void VulkanSceneRuntime::DiscardPreparedFrame()
{
    // Acquire失敗時に残った参照をSwapChain再生成やDevice破棄前に解放します。
    m_PreparedFrame.reset();
}

bool VulkanSceneRuntime::PrepareFrame()
{
    m_PreparedFrame.reset();
    if (m_Initialized == false || m_Device == nullptr ||
        m_OpaquePipeline == nullptr || m_TransparentPipeline == nullptr ||
        m_DebugLinePipeline == nullptr || m_DefaultTexture == nullptr ||
        m_Context.GetActiveCommandBuffer() != VK_NULL_HANDLE)
    {
        return false;
    }

    auto prepared = CreateScope<Renderer::PreparedRHISceneFrame>();
    if (Renderer::PrepareRHISceneFrame(*m_Device, m_OpaquePipeline,
        m_TransparentPipeline, m_DefaultTexture,
        RHISceneDrawItemBuilder::VulkanClipCorrection(), *prepared) == false ||
        Renderer::PrepareRHIDebugLines(*m_Device, m_DefaultTexture,
            m_DebugLinePipeline, RHISceneDrawItemBuilder::VulkanClipCorrection(),
            prepared->DebugLineItems) == false)
    {
        return false;
    }
    prepared->DebugLinePipeline = m_DebugLinePipeline;
    m_PreparedFrame = std::move(prepared);
    return true;
}

RHIFrameResult VulkanSceneRuntime::DrawPreparedFrame()
{
    if (m_Initialized == false || m_PreparedFrame == nullptr ||
        m_Context.GetActiveCommandBuffer() == VK_NULL_HANDLE)
    {
        return RHIFrameResult::FatalError;
    }
    VulkanSceneCommandList commands(m_Context);
    const RHIFrameResult result = Renderer::DrawPreparedRHISceneFrame(
        m_Context, commands, *m_PreparedFrame);
    // GPUが参照するBufferはContext側がFence完了まで保持します。
    m_PreparedFrame.reset();
    // FatalErrorでもRuntimeを破棄せず、所有元Applicationの終了処理へ委譲します。
    return result;
}

RHIFrameResult VulkanSceneRuntime::DrawFrame()
{
    if (PrepareFrame() == false)
    {
        return RHIFrameResult::FatalError;
    }
    const RHIFrameResult begin = m_Context.BeginFrame();
    if (begin != RHIFrameResult::Success)
    {
        m_PreparedFrame.reset();
        return begin;
    }
    return DrawPreparedFrame();
}

bool VulkanSceneRuntime::Resize(uint32_t width, uint32_t height)
{
    if (m_Initialized == false || width == 0 || height == 0)
    {
        return false;
    }

    // 再生成前に旧Frameの保持参照を外します。失敗時も所有元がShutdownします。
    m_PreparedFrame.reset();
    if (m_Context.Resize(width, height) == false)
    {
        return false;
    }

    // Context::Resize()が旧RenderPass用native Pipelineを無効化しています。
    // 外部Refも破棄し、新しいRender Target情報から両Pipelineを再生成します。
    m_OpaquePipeline.reset();
    m_TransparentPipeline.reset();
    m_DebugLinePipeline.reset();
    if (CreatePipelines() == false)
    {
        // Context/Deviceは維持し、Applicationに失敗を返して終了順序を守ります。
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

    // 準備済みFrameが保持する旧DeviceのBuffer参照を最初に解放します。
    m_PreparedFrame.reset();
    // Contextより先に上位Resource参照を解放します。
    // Context自身も外部Refが残ったResourceをDevice破棄前に無効化します。
    m_OpaquePipeline.reset();
    m_TransparentPipeline.reset();
    m_DebugLinePipeline.reset();
    m_DefaultTexture.reset();
    m_PipelineSpecification = {};
    m_PipelineDebugName.clear();
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

RHISceneFrameLifecycle* VulkanSceneRuntime::GetFrameLifecycle()
{
    return m_Initialized == true ? &m_Context : nullptr;
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
    if (m_Device == nullptr ||
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
        m_PipelineSpecification,
        m_VertexShader->GetBinary(),
        m_FragmentShader->GetBinary(),
        opaque,
        transparent) == false)
    {
        return false;
    }

    Ref<RHIGraphicsPipeline> debugLine;
    if (Renderer::CreateRHIDebugLinePipeline(
        *m_Device, m_VertexShader->GetBinary(),
        m_FragmentShader->GetBinary(), debugLine) == false)
    {
        return false;
    }

    // Surface/Debugの全Pipelineが揃ってから差し替え、Resize途中の部分状態を公開しません。
    m_OpaquePipeline = std::move(opaque);
    m_TransparentPipeline = std::move(transparent);
    m_DebugLinePipeline = std::move(debugLine);
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
