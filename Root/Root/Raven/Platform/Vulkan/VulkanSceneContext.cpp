#include "VulkanSceneContext.h"

#include "Raven/Core/Window.h"

#include <GLFW/glfw3.h>

namespace Raven
{
namespace
{
RHIFrameResult ToSceneFrameResult(VulkanFrameResult result)
{
    if (result == VulkanFrameResult::Success)
    {
        return RHIFrameResult::Success;
    }
    if (result == VulkanFrameResult::ResizeRequired)
    {
        return RHIFrameResult::ResizeRequired;
    }
    return RHIFrameResult::FatalError;
}
} // namespace

VulkanSceneContext::~VulkanSceneContext()
{
    Shutdown();
}

bool VulkanSceneContext::Init(Window& window)
{
    Shutdown();
    if (window.GetBackend() != RHIBackend::Vulkan ||
        window.GetNativeWindow() == nullptr)
    {
        return false;
    }

    int width = 0;
    int height = 0;
    GLFWwindow* nativeWindow = static_cast<GLFWwindow*>(window.GetNativeWindow());
    glfwGetFramebufferSize(nativeWindow, &width, &height);
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    m_VSync = window.IsVSync();
    if (m_Instance.Init() == false ||
        m_Surface.Init(m_Instance.GetHandle(), nativeWindow) == false ||
        m_SwapChain.Init(m_Instance.GetDevice(), m_Surface.GetHandle(),
            static_cast<uint32_t>(width), static_cast<uint32_t>(height), m_VSync) == false ||
        m_FrameSync.Init(m_Instance.GetDevice(),
            static_cast<uint32_t>(m_SwapChain.GetImages().size())) == false ||
        m_RenderTarget.Init(m_Instance.GetDevice().GetHandle(), m_SwapChain) == false)
    {
        Shutdown();
        return false;
    }

    // Fence完了前のCommandPool再利用を避けるため、Frame Slotごとに確保します。
    for (uint32_t frame = 0; frame < m_FrameSync.GetFrameCount(); ++frame)
    {
        auto commandBuffer = std::make_unique<VulkanCommandBuffer>();
        if (commandBuffer->Init(m_Instance.GetDevice()) == false)
        {
            Shutdown();
            return false;
        }
        m_CommandBuffers.push_back(std::move(commandBuffer));
    }
    m_ClearColor.float32[3] = 1.0f;
    return true;
}

RHIFrameResult VulkanSceneContext::BeginFrame()
{
    if (m_FrameActive == true || m_Instance.IsValid() == false ||
        m_RenderTarget.IsValid() == false)
    {
        return RHIFrameResult::FatalError;
    }

    const uint32_t frame = m_FrameSync.GetCurrentFrameIndex();
    if (frame >= m_CommandBuffers.size() || m_CommandBuffers[frame] == nullptr)
    {
        return RHIFrameResult::FatalError;
    }

    VulkanCommandBuffer& commandBuffer = *m_CommandBuffers[frame];
    const VulkanFrameResult acquire = m_FrameRenderer.BeginFrame(
        m_Instance.GetDevice(), m_SwapChain, commandBuffer, m_FrameSync);
    if (acquire != VulkanFrameResult::Success)
    {
        return ToSceneFrameResult(acquire);
    }

    // Acquire成功後に記録が失敗した場合はSemaphoreが未消費になり得ます。
    // FatalError後は同じContextで次Frameを開始せず、Shutdownして再生成します。
    m_FrameActive = true;
    m_ActiveFrame = frame;
    if (m_FrameRenderer.BeginSceneColorTarget(m_SwapChain, commandBuffer) !=
        VulkanFrameResult::Success)
    {
        return RHIFrameResult::FatalError;
    }
    if (m_RenderTarget.Begin(commandBuffer.GetHandle(),
        m_FrameRenderer.GetAcquiredImageIndex(), m_ClearColor) == false)
    {
        return RHIFrameResult::FatalError;
    }
    m_FrameSubmitted = false;
    m_BoundGraphicsPipeline.reset();
    const VkExtent2D extent = m_SwapChain.GetExtent();
    // Resize直後も初回Draw前に有効なViewport/Scissorを記録します。
    if (SetViewport(0, 0, extent.width, extent.height) == false)
    {
        return RHIFrameResult::FatalError;
    }
    return RHIFrameResult::Success;
}

RHIFrameResult VulkanSceneContext::EndFrame()
{
    if (m_FrameActive == false || m_FrameSubmitted == true ||
        m_ActiveFrame >= m_CommandBuffers.size())
    {
        return RHIFrameResult::FatalError;
    }

    VulkanCommandBuffer& commandBuffer = *m_CommandBuffers[m_ActiveFrame];
    m_BoundGraphicsPipeline.reset();
    m_RenderTarget.End(commandBuffer.GetHandle());
    if (m_FrameRenderer.EndSceneColorTarget(m_SwapChain, commandBuffer) !=
        VulkanFrameResult::Success)
    {
        return RHIFrameResult::FatalError;
    }
    const VulkanFrameResult result = m_FrameRenderer.EndFrame(
        m_Instance.GetDevice(), m_SwapChain, commandBuffer, m_FrameSync);
    if (result == VulkanFrameResult::Success)
    {
        m_FrameSubmitted = true;
    }
    return ToSceneFrameResult(result);
}

RHIFrameResult VulkanSceneContext::Present()
{
    if (m_FrameActive == false || m_FrameSubmitted == false)
    {
        return RHIFrameResult::FatalError;
    }

    const VulkanFrameResult result = m_FrameRenderer.Present(
        m_Instance.GetDevice(), m_SwapChain, m_FrameSync);
    // Present内部でFrame Slotが進むため、ResizeRequiredでも次Frameを開始できます。
    // FatalError後はContextを再利用せずShutdownします。
    m_FrameActive = false;
    m_FrameSubmitted = false;
    return ToSceneFrameResult(result);
}

bool VulkanSceneContext::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || m_FrameActive == true ||
        m_Instance.IsValid() == false)
    {
        return false;
    }

    // Framebuffer/ImageViewは旧SwapChain Imageを参照するため、必ず先に破棄します。
    if (m_Instance.GetDevice().WaitIdle() == false)
    {
        return false;
    }
    m_BoundGraphicsPipeline.reset();
    // RenderPass再生成後に古いPipelineをBindしないようnative handleを無効化します。
    for (const auto& pipeline : m_GraphicsPipelines)
    {
        if (pipeline != nullptr)
        {
            pipeline->Shutdown();
        }
    }
    m_GraphicsPipelines.clear();
    m_RenderTarget.Shutdown();
    if (m_SwapChain.Recreate(width, height, m_VSync) == false ||
        m_FrameSync.Init(m_Instance.GetDevice(),
            static_cast<uint32_t>(m_SwapChain.GetImages().size())) == false ||
        m_RenderTarget.Init(m_Instance.GetDevice().GetHandle(), m_SwapChain) == false)
    {
        return false;
    }
    return true;
}

bool VulkanSceneContext::SetViewport(
    uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    const VkExtent2D extent = m_SwapChain.GetExtent();
    if (width == 0 || height == 0 || x >= extent.width || y >= extent.height ||
        width > extent.width - x || height > extent.height - y)
    {
        return false;
    }

    VkCommandBuffer commandBuffer = GetActiveCommandBuffer();
    if (commandBuffer == VK_NULL_HANDLE)
    {
        return false;
    }

    // VulkanのViewportはFramebuffer左上を原点にし、PipelineのDynamic Stateと対応させます。
    VkViewport viewport{};
    viewport.x = static_cast<float>(x);
    viewport.y = static_cast<float>(y);
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.offset = { static_cast<int32_t>(x), static_cast<int32_t>(y) };
    scissor.extent = { width, height };
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    return true;
}

void VulkanSceneContext::SetClearColor(const float color[4])
{
    if (color == nullptr)
    {
        return;
    }
    for (uint32_t component = 0; component < 4; ++component)
    {
        m_ClearColor.float32[component] = color[component];
    }
}

VkCommandBuffer VulkanSceneContext::GetActiveCommandBuffer() const
{
    if (m_FrameActive == false || m_FrameSubmitted == true ||
        m_ActiveFrame >= m_CommandBuffers.size() ||
        m_CommandBuffers[m_ActiveFrame] == nullptr)
    {
        return VK_NULL_HANDLE;
    }
    return m_CommandBuffers[m_ActiveFrame]->GetHandle();
}

Ref<RHIGraphicsPipeline> VulkanSceneContext::CreateGraphicsPipeline(
    const RHIGraphicsPipelineSpecification& specification)
{
    if (m_FrameActive == true || m_Instance.IsValid() == false ||
        m_RenderTarget.IsValid() == false ||
        specification.IsValidForBackend(RHIBackend::Vulkan) == false)
    {
        return nullptr;
    }

    auto pipeline = CreateRef<VulkanGraphicsPipeline>();
    if (pipeline->Init(m_Instance.GetDevice().GetHandle(),
        m_RenderTarget.GetRenderPass(), m_SwapChain.GetImageFormat(), specification) == false)
    {
        return nullptr;
    }
    m_GraphicsPipelines.push_back(pipeline);
    return pipeline;
}

bool VulkanSceneContext::BindGraphicsPipeline(const Ref<RHIGraphicsPipeline>& pipeline)
{
    VkCommandBuffer commandBuffer = GetActiveCommandBuffer();
    if (commandBuffer == VK_NULL_HANDLE || pipeline == nullptr)
    {
        return false;
    }
    auto nativePipeline = std::dynamic_pointer_cast<VulkanGraphicsPipeline>(pipeline);
    if (nativePipeline == nullptr || nativePipeline->IsValid() == false)
    {
        return false;
    }

    // 別のVkDevice/RenderPassで生成されたPipelineをこのSceneへBindさせません。
    bool owned = false;
    for (const auto& registeredPipeline : m_GraphicsPipelines)
    {
        if (registeredPipeline == nativePipeline)
        {
            owned = true;
            break;
        }
    }
    if (owned == false)
    {
        return false;
    }
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        nativePipeline->GetHandle());
    m_BoundGraphicsPipeline = nativePipeline;
    return true;
}

bool VulkanSceneContext::DrawIndexed(const VulkanSceneBuffer& vertexBuffer,
    const VulkanSceneBuffer& indexBuffer, uint32_t indexCount,
    bool usePipelineVertexStride)
{
    VkCommandBuffer commandBuffer = GetActiveCommandBuffer();
    if (commandBuffer == VK_NULL_HANDLE || m_BoundGraphicsPipeline == nullptr ||
        m_BoundGraphicsPipeline->IsValid() == false ||
        vertexBuffer.IsValid() == false || indexBuffer.IsValid() == false ||
        vertexBuffer.IsIndexBuffer() == true || indexBuffer.IsIndexBuffer() == false ||
        vertexBuffer.GetDeviceHandle() != m_Instance.GetDevice().GetHandle() ||
        indexBuffer.GetDeviceHandle() != m_Instance.GetDevice().GetHandle())
    {
        return false;
    }
    const uint32_t drawCount = indexCount == 0 ? indexBuffer.GetIndexCount() : indexCount;
    if (drawCount == 0 || drawCount > indexBuffer.GetIndexCount())
    {
        return false;
    }

    // 現在のSceneBufferはVertex 1本、uint32_t Indexのみをサポートします。
    // Pipelineの入力宣言とBufferのStrideが一致することを記録前に確認します。
    const auto& bindings = m_BoundGraphicsPipeline->GetSpecification().VertexBindings;
    // 共通RHIBufferのVertexはbyte単位で確保されるため、描画に必要なStrideは
    // 現在Bind中のPipelineのBinding 0から取得します。native経路は保持Strideを検証します。
    if (bindings.size() != 1 || bindings[0].Binding != 0 ||
        bindings[0].Stride == 0)
    {
        return false;
    }
    const uint32_t vertexStride = usePipelineVertexStride == true ?
        bindings[0].Stride : vertexBuffer.GetVertexStride();
    if (vertexStride == 0 ||
        (usePipelineVertexStride == true && vertexBuffer.GetVertexStride() != 1) ||
        vertexBuffer.GetCapacity() % vertexStride != 0 ||
        bindings[0].Stride != vertexStride)
    {
        return false;
    }
    const VkBuffer vertex = vertexBuffer.GetHandle();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertex, &offset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer.GetHandle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, drawCount, 1, 0, 0, 0);
    return true;
}

void VulkanSceneContext::Shutdown()
{
    if (m_Instance.IsValid() == true)
    {
        m_Instance.GetDevice().WaitIdle();
    }
    m_FrameActive = false;
    m_FrameSubmitted = false;
    m_ActiveFrame = 0;
    m_BoundGraphicsPipeline.reset();
    // 外部Refが残っていてもVkDevice破棄後のDestructorでVulkanを呼ばせません。
    for (const auto& pipeline : m_GraphicsPipelines)
    {
        if (pipeline != nullptr)
        {
            pipeline->Shutdown();
        }
    }
    m_GraphicsPipelines.clear();
    m_FrameSync.Shutdown();
    m_CommandBuffers.clear();
    m_RenderTarget.Shutdown();
    m_SwapChain.Shutdown();
    m_Surface.Shutdown();
    m_Instance.Shutdown();
}
} // namespace Raven
