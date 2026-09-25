#include "VulkanSceneContext.h"
#include "VulkanSceneRHIBuffer.h"
#include "VulkanSceneRHITexture.h"

#include "Raven/Core/Window.h"

#include <GLFW/glfw3.h>

#include <limits>

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
        m_RenderTarget.Init(m_Instance.GetDevice().GetHandle(),
            m_Instance.GetDevice().GetPhysicalDeviceHandle(), m_SwapChain) == false)
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
    m_RecordedBuffers.resize(m_FrameSync.GetFrameCount());
    m_SubmittedBufferFrames.assign(m_FrameSync.GetFrameCount(), false);
    m_ClearColor.float32[3] = 1.0f;
    return true;
}

RHIFrameResult VulkanSceneContext::BeginFrame()
{
    if (m_FatalError == true || m_FrameActive == true || m_Instance.IsValid() == false ||
        m_RenderTarget.IsValid() == false)
    {
        m_FatalError = true;
        return RHIFrameResult::FatalError;
    }

    const uint32_t frame = m_FrameSync.GetCurrentFrameIndex();
    if (frame >= m_CommandBuffers.size() || m_CommandBuffers[frame] == nullptr)
    {
        m_FatalError = true;
        return RHIFrameResult::FatalError;
    }

    VulkanCommandBuffer& commandBuffer = *m_CommandBuffers[frame];
    const VulkanFrameResult acquire = m_FrameRenderer.BeginFrame(
        m_Instance.GetDevice(), m_SwapChain, commandBuffer, m_FrameSync);
    if (acquire != VulkanFrameResult::Success)
    {
        if (acquire == VulkanFrameResult::FatalError)
        {
            m_FatalError = true;
        }
        return ToSceneFrameResult(acquire);
    }

    // BeginFrame内部でこのSlotのFence待機が完了しています。
    // 前回SubmitのBufferを解放してから、新しいDrawを記録します。
    if (frame >= m_RecordedBuffers.size() ||
        frame >= m_SubmittedBufferFrames.size())
    {
        m_FatalError = true;
        return RHIFrameResult::FatalError;
    }
    m_RecordedBuffers[frame].clear();
    m_SubmittedBufferFrames[frame] = false;

    // Acquire成功後に記録が失敗した場合はSemaphoreが未消費になり得ます。
    // FatalError後は同じContextで次Frameを開始せず、Shutdownして再生成します。
    m_FrameActive = true;
    m_ActiveFrame = frame;
    if (m_FrameRenderer.BeginSceneColorTarget(m_SwapChain, commandBuffer) !=
        VulkanFrameResult::Success)
    {
        m_FatalError = true;
        return RHIFrameResult::FatalError;
    }
    if (m_RenderTarget.Begin(commandBuffer.GetHandle(),
        m_FrameRenderer.GetAcquiredImageIndex(), m_ClearColor) == false)
    {
        m_FatalError = true;
        return RHIFrameResult::FatalError;
    }
    m_FrameSubmitted = false;
    m_BoundGraphicsPipeline.reset();
    const VkExtent2D extent = m_SwapChain.GetExtent();
    // Resize直後も初回Draw前に有効なViewport/Scissorを記録します。
    if (SetViewport(0, 0, extent.width, extent.height) == false)
    {
        m_FatalError = true;
        return RHIFrameResult::FatalError;
    }
    return RHIFrameResult::Success;
}

RHIFrameResult VulkanSceneContext::EndFrame()
{
    if (m_FatalError == true || m_FrameActive == false || m_FrameSubmitted == true ||
        m_ActiveFrame >= m_CommandBuffers.size())
    {
        m_FatalError = true;
        return RHIFrameResult::FatalError;
    }

    VulkanCommandBuffer& commandBuffer = *m_CommandBuffers[m_ActiveFrame];
    m_BoundGraphicsPipeline.reset();
    m_RenderTarget.End(commandBuffer.GetHandle());
    if (m_FrameRenderer.EndSceneColorTarget(m_SwapChain, commandBuffer) !=
        VulkanFrameResult::Success)
    {
        m_FatalError = true;
        return RHIFrameResult::FatalError;
    }
    const VulkanFrameResult result = m_FrameRenderer.EndFrame(
        m_Instance.GetDevice(), m_SwapChain, commandBuffer, m_FrameSync);
    if (result == VulkanFrameResult::Success)
    {
        m_FrameSubmitted = true;
        // Submit成功時だけFenceを待機対象にします。
        m_SubmittedBufferFrames[m_ActiveFrame] = true;
    }
    if (result == VulkanFrameResult::FatalError)
    {
        m_FatalError = true;
    }
    return ToSceneFrameResult(result);
}

RHIFrameResult VulkanSceneContext::Present()
{
    if (m_FatalError == true || m_FrameActive == false || m_FrameSubmitted == false)
    {
        m_FatalError = true;
        return RHIFrameResult::FatalError;
    }

    const VulkanFrameResult result = m_FrameRenderer.Present(
        m_Instance.GetDevice(), m_SwapChain, m_FrameSync);
    // Present内部でFrame Slotが進むため、ResizeRequiredでも次Frameを開始できます。
    // FatalError後はContextを再利用せずShutdownします。
    m_FrameActive = false;
    m_FrameSubmitted = false;
    if (result == VulkanFrameResult::FatalError)
    {
        m_FatalError = true;
    }
    return ToSceneFrameResult(result);
}

bool VulkanSceneContext::Resize(uint32_t width, uint32_t height)
{
    if (m_FatalError == true || width == 0 || height == 0 || m_FrameActive == true ||
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
    // Descriptor Setは旧Pipeline Layoutに対応するため、Pipeline無効化より先に破棄します。
    DestroyTextureDescriptors();
    m_RecordedBuffers.clear();
    m_SubmittedBufferFrames.clear();
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
        m_RenderTarget.Init(m_Instance.GetDevice().GetHandle(),
            m_Instance.GetDevice().GetPhysicalDeviceHandle(), m_SwapChain) == false)
    {
        return false;
    }
    m_RecordedBuffers.resize(m_FrameSync.GetFrameCount());
    m_SubmittedBufferFrames.assign(m_FrameSync.GetFrameCount(), false);
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

bool VulkanSceneContext::ClearColorAttachment(const float color[4])
{
    // vkCmdClearAttachmentsはRenderPass内でのみ有効です。
    // Frame開始時のLoadOp Clearと異なり、Scene描画途中のClearを明示的に記録します。
    VkCommandBuffer commandBuffer = GetActiveCommandBuffer();
    const VkExtent2D extent = m_SwapChain.GetExtent();
    if (color == nullptr || commandBuffer == VK_NULL_HANDLE ||
        m_RenderTarget.IsValid() == false || extent.width == 0 || extent.height == 0)
    {
        return false;
    }

    VkClearAttachment attachment{};
    attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    attachment.colorAttachment = 0;
    for (uint32_t component = 0; component < 4; ++component)
    {
        attachment.clearValue.color.float32[component] = color[component];
    }

    // Viewport/ScissorはClear範囲を制限しません。SwapChain全体を消去します。
    VkClearRect rectangle{};
    rectangle.rect.offset = { 0, 0 };
    rectangle.rect.extent = extent;
    rectangle.baseArrayLayer = 0;
    rectangle.layerCount = 1;
    vkCmdClearAttachments(commandBuffer, 1, &attachment, 1, &rectangle);
    return true;
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

bool VulkanSceneContext::BindTextureDescriptor(VkDescriptorSet descriptorSet)
{
    VkCommandBuffer commandBuffer = GetActiveCommandBuffer();
    if (commandBuffer == VK_NULL_HANDLE || descriptorSet == VK_NULL_HANDLE ||
        m_BoundGraphicsPipeline == nullptr ||
        m_BoundGraphicsPipeline->IsValid() == false)
    {
        return false;
    }
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_BoundGraphicsPipeline->GetLayout(), 0, 1, &descriptorSet,
        0, nullptr);
    return true;
}

bool VulkanSceneContext::RebuildTextureDescriptors(
    const std::vector<Ref<RHITexture>>& textures, const Ref<RHIGraphicsPipeline>& pipeline)
{
    if (textures.empty() == true || pipeline == nullptr ||
        GetDevice().IsValid() == false ||
        textures.size() > std::numeric_limits<uint32_t>::max())
    {
        return false;
    }
    if (GetActiveCommandBuffer() != VK_NULL_HANDLE)
    {
        return false;
    }
    const auto native = std::dynamic_pointer_cast<VulkanGraphicsPipeline>(pipeline);
    if (native == nullptr || native->GetTextureSetLayout() == VK_NULL_HANDLE)
    {
        return false;
    }

    bool sameTextures = textures.size() == m_DescriptorTextures.size();
    if (sameTextures == true)
    {
        for (std::size_t index = 0; index < textures.size(); ++index)
        {
            if (textures[index] != m_DescriptorTextures[index])
            {
                sameTextures = false;
                break;
            }
        }
    }
    if (m_TextureDescriptorPool != VK_NULL_HANDLE &&
        m_TextureDescriptorPipeline == native && sameTextures == true)
    {
        return true;
    }

    // Poolを差し替えるため、旧Descriptorを参照するGPU仕事の完了を確認します。
    if (m_TextureDescriptorPool != VK_NULL_HANDLE && GetDevice().WaitIdle() == false)
    {
        return false;
    }
    // 既存Poolを残したまま新Poolを構築し、途中失敗でも既存Meshの描画を維持します。
    // 旧Descriptorを参照するGPU処理は上記WaitIdleで完了済みです。
    const VkDevice device = GetDevice().GetHandle();
    VkDescriptorPool newPool = VK_NULL_HANDLE;
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = static_cast<uint32_t>(textures.size());
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = static_cast<uint32_t>(textures.size());
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &newPool) != VK_SUCCESS)
    {
        return false;
    }
    const VkDescriptorSetLayout layout = native->GetTextureSetLayout();
    std::vector<VkDescriptorSetLayout> layouts(textures.size(), layout);
    std::vector<VkDescriptorSet> descriptors(textures.size(), VK_NULL_HANDLE);
    VkDescriptorSetAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocation.descriptorPool = newPool;
    allocation.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocation.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &allocation, descriptors.data()) != VK_SUCCESS)
    {
        vkDestroyDescriptorPool(device, newPool, nullptr);
        return false;
    }
    for (std::size_t index = 0; index < textures.size(); ++index)
    {
        const auto nativeTexture =
            std::dynamic_pointer_cast<VulkanSceneRHITexture>(textures[index]);
        if (nativeTexture == nullptr ||
            nativeTexture->GetNativeTexture().IsValid() == false)
        {
            vkDestroyDescriptorPool(device, newPool, nullptr);
            return false;
        }
        VkDescriptorImageInfo image{};
        image.sampler = nativeTexture->GetNativeTexture().GetSampler();
        image.imageView = nativeTexture->GetNativeTexture().GetView();
        image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptors[index];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    // すべて成功した場合だけ旧Poolを破棄し、登録済みTextureのBindingを切り替えます。
    DestroyTextureDescriptors();
    m_TextureDescriptorPool = newPool;
    m_TextureDescriptorPipeline = native;
    m_TextureDescriptors.resize(descriptors.size());
    m_DescriptorTextures = textures;
    for (std::size_t index = 0; index < textures.size(); ++index)
    {
        m_TextureDescriptors[index] = descriptors[index];
    }
    return true;
}

void VulkanSceneContext::DestroyTextureDescriptors()
{
    if (m_TextureDescriptorPool != VK_NULL_HANDLE &&
        GetDevice().IsValid() == true)
    {
        vkDestroyDescriptorPool(GetDevice().GetHandle(),
            m_TextureDescriptorPool, nullptr);
    }
    m_TextureDescriptorPool = VK_NULL_HANDLE;
    m_TextureDescriptorPipeline.reset();
    m_TextureDescriptors.clear();
    m_DescriptorTextures.clear();
}

bool VulkanSceneContext::BindTexture(const Ref<RHITexture>& texture)
{
    if (texture == nullptr)
    {
        return false;
    }
    for (std::size_t index = 0; index < m_DescriptorTextures.size(); ++index)
    {
        if (m_DescriptorTextures[index] == texture)
        {
            return BindTextureDescriptor(m_TextureDescriptors[index]);
        }
    }
    return false;
}

bool VulkanSceneContext::SetMaterialTint(const std::array<float, 4>& tint)
{
    VkCommandBuffer commandBuffer = GetActiveCommandBuffer();
    if (commandBuffer == VK_NULL_HANDLE || m_BoundGraphicsPipeline == nullptr ||
        m_BoundGraphicsPipeline->IsValid() == false)
    {
        return false;
    }
    // Vertex用64byteと重ならないFragment専用領域へ書き込みます。
    vkCmdPushConstants(commandBuffer, m_BoundGraphicsPipeline->GetLayout(),
        VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(float) * 16,
        static_cast<uint32_t>(sizeof(float) * tint.size()), tint.data());
    return true;
}

bool VulkanSceneContext::SetClipTransform(const std::array<float, 16>& model)
{
    VkCommandBuffer commandBuffer = GetActiveCommandBuffer();
    if (commandBuffer == VK_NULL_HANDLE || m_BoundGraphicsPipeline == nullptr ||
        m_BoundGraphicsPipeline->IsValid() == false)
    {
        return false;
    }
    // Model/View/Projectionを合成した行列をCommand Bufferへコピーします。
    vkCmdPushConstants(commandBuffer, m_BoundGraphicsPipeline->GetLayout(),
        VK_SHADER_STAGE_VERTEX_BIT, 0, static_cast<uint32_t>(sizeof(float) * model.size()),
        model.data());
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

bool VulkanSceneContext::SynchronizeBufferAccess(
    const VulkanSceneRHIBuffer& buffer)
{
    // Host CoherentはGPUとの競合を防ぎません。Frame外で、更新対象Bufferを
    // 実際に参照したSubmit済みSlotだけを調べ、対応するFenceを待機します。
    if (m_Instance.IsValid() == false || m_FrameActive == true ||
        m_FrameSubmitted == true || m_FrameSync.IsValid() == false ||
        m_RecordedBuffers.size() != m_FrameSync.GetFrameCount() ||
        m_SubmittedBufferFrames.size() != m_FrameSync.GetFrameCount())
    {
        return false;
    }

    std::vector<VkFence> fences;
    for (uint32_t frame = 0; frame < m_FrameSync.GetFrameCount(); ++frame)
    {
        if (m_SubmittedBufferFrames[frame] == false)
        {
            continue;
        }

        // Frame内で同じBufferを何度Drawしてもキーは一度だけ登録されます。
        // 強参照がキーのResource寿命を保持するため、アドレスの再利用は起きません。
        if (m_RecordedBuffers[frame].find(&buffer) ==
            m_RecordedBuffers[frame].end())
        {
            continue;
        }

        const VkFence fence = m_FrameSync.GetFenceForFrame(frame);
        if (fence == VK_NULL_HANDLE)
        {
            return false;
        }
        fences.push_back(fence);
    }

    if (fences.empty() == false &&
        vkWaitForFences(m_Instance.GetDevice().GetHandle(),
            static_cast<uint32_t>(fences.size()), fences.data(), VK_TRUE,
            std::numeric_limits<uint64_t>::max()) != VK_SUCCESS)
    {
        return false;
    }

    // 他のBufferは同じSlotでまだGPU使用中の可能性があるため、
    // Slot全体の参照とSubmit状態は変更しません。BeginFrameのFence待機後に回収します。
    return true;
}

void VulkanSceneContext::RetainDrawBuffers(
    const Ref<RHIBuffer>& vertex, const Ref<RHIBuffer>& index)
{
    if (m_FrameActive == false || m_ActiveFrame >= m_RecordedBuffers.size())
    {
        return;
    }
    // 同一Bufferが複数DrawやVertex/Indexの両方で指定されても、
    // Frame Slot内で強参照は一つだけ保持し、Fence検索を重複させません。
    auto& recorded = m_RecordedBuffers[m_ActiveFrame];
    if (vertex != nullptr && recorded.find(vertex.get()) == recorded.end())
    {
        recorded.emplace(vertex.get(), vertex);
    }
    if (index != nullptr && recorded.find(index.get()) == recorded.end())
    {
        recorded.emplace(index.get(), index);
    }
}

void VulkanSceneContext::RegisterBuffer(const Ref<VulkanSceneRHIBuffer>& buffer)
{
    if (buffer == nullptr)
    {
        return;
    }
    // 破棄済みのweak参照は登録時に整理し、長時間のBuffer生成でも増加を抑えます。
    for (auto iterator = m_Buffers.begin(); iterator != m_Buffers.end();)
    {
        if (iterator->expired() == true)
        {
            iterator = m_Buffers.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
    m_Buffers.push_back(buffer);
}

void VulkanSceneContext::RegisterTexture(
    const Ref<VulkanSceneRHITexture>& texture)
{
    if (texture == nullptr)
    {
        return;
    }
    // 破棄済みTextureの弱参照を整理し、Contextは所有権を奪いません。
    for (auto iterator = m_Textures.begin(); iterator != m_Textures.end();)
    {
        if (iterator->expired() == true)
        {
            iterator = m_Textures.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
    m_Textures.push_back(texture);
}

void VulkanSceneContext::Shutdown()
{
    if (m_Instance.IsValid() == true)
    {
        m_Instance.GetDevice().WaitIdle();
    }
    // Device破棄前に、外部がRefを保持しているBufferもnative handleを解放します。
    // WaitIdle済みであることを前提にするため、Buffer側で再度同期しません。
    for (const auto& weakBuffer : m_Buffers)
    {
        auto buffer = weakBuffer.lock();
        if (buffer != nullptr)
        {
            buffer->InvalidateAfterDeviceIdle();
        }
    }
    m_Buffers.clear();
    // Descriptorが参照するImageView/Samplerより先にPoolを解放します。
    DestroyTextureDescriptors();
    // VkDeviceを破棄する前に外部RefのVkImage/ImageView/Samplerを解放します。
    for (const auto& weakTexture : m_Textures)
    {
        auto texture = weakTexture.lock();
        if (texture != nullptr)
        {
            texture->Shutdown();
        }
    }
    m_Textures.clear();
    m_RecordedBuffers.clear();
    m_SubmittedBufferFrames.clear();
    m_FrameActive = false;
    m_FrameSubmitted = false;
    m_FatalError = false;
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
