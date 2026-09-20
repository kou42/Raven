#pragma once

#include "VulkanDevice.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstring>
#include <limits>

namespace Raven
{

// Scene用のRGBA8 Texture。InitはFrame外で呼び、転送完了を待ってから返します。
// Descriptor Setの所有・Bindは次段階で実装します。Device破棄前にShutdownしてください。
class VulkanSceneTexture final
{
public:
    VulkanSceneTexture() = default;
    ~VulkanSceneTexture() { Shutdown(); }
    VulkanSceneTexture(const VulkanSceneTexture&) = delete;
    VulkanSceneTexture& operator=(const VulkanSceneTexture&) = delete;

    bool Init(const VulkanDevice& device, uint32_t width, uint32_t height,
        const uint8_t* rgba)
    {
        Shutdown();
        if (device.IsValid() == false || rgba == nullptr ||
            width == 0 || height == 0 ||
            static_cast<uint64_t>(width) * height >
                std::numeric_limits<VkDeviceSize>::max() / 4 ||
            static_cast<uint64_t>(width) * height >
                std::numeric_limits<size_t>::max() / 4)
        {
            return false;
        }
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDeviceHandle(),
            VK_FORMAT_R8G8B8A8_UNORM, &properties);
        if ((properties.optimalTilingFeatures &
            (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) !=
            (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        {
            return false;
        }
        m_Device = device.GetHandle();
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        VkCommandPool pool = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        bool success = false;
        do
        {
            VkBufferCreateInfo buffer{};
            buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer.size = bytes;
            buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(m_Device, &buffer, nullptr, &staging) != VK_SUCCESS)
            {
                break;
            }
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(m_Device, staging, &requirements);
            const uint32_t hostType = FindMemoryType(device, requirements,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (hostType == UINT32_MAX)
            {
                break;
            }
            VkMemoryAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = hostType;
            if (vkAllocateMemory(m_Device, &allocation, nullptr,
                &stagingMemory) != VK_SUCCESS ||
                vkBindBufferMemory(m_Device, staging, stagingMemory, 0) != VK_SUCCESS)
            {
                break;
            }
            void* mapped = nullptr;
            if (vkMapMemory(m_Device, stagingMemory, 0, bytes, 0,
                &mapped) != VK_SUCCESS || mapped == nullptr)
            {
                break;
            }
            std::memcpy(mapped, rgba, static_cast<size_t>(bytes));
            vkUnmapMemory(m_Device, stagingMemory);

            VkImageCreateInfo image{};
            image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image.imageType = VK_IMAGE_TYPE_2D;
            image.format = VK_FORMAT_R8G8B8A8_UNORM;
            image.extent = {width, height, 1};
            image.mipLevels = 1;
            image.arrayLayers = 1;
            image.samples = VK_SAMPLE_COUNT_1_BIT;
            image.tiling = VK_IMAGE_TILING_OPTIMAL;
            image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT;
            image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(m_Device, &image, nullptr, &m_Image) != VK_SUCCESS)
            {
                break;
            }
            vkGetImageMemoryRequirements(m_Device, m_Image, &requirements);
            const uint32_t imageType = FindMemoryType(device, requirements,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (imageType == UINT32_MAX)
            {
                break;
            }
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = imageType;
            if (vkAllocateMemory(m_Device, &allocation, nullptr,
                &m_Memory) != VK_SUCCESS ||
                vkBindImageMemory(m_Device, m_Image, m_Memory, 0) != VK_SUCCESS)
            {
                break;
            }

            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = device.GetGraphicsQueueFamilyIndex();
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            if (vkCreateCommandPool(m_Device, &poolInfo, nullptr,
                &pool) != VK_SUCCESS)
            {
                break;
            }
            VkCommandBuffer command = VK_NULL_HANDLE;
            VkCommandBufferAllocateInfo commandInfo{};
            commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandInfo.commandPool = pool;
            commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandInfo.commandBufferCount = 1;
            if (vkAllocateCommandBuffers(m_Device, &commandInfo,
                &command) != VK_SUCCESS)
            {
                break;
            }
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS)
            {
                break;
            }
            // 転送前後のLayoutと可視性を明示し、Fragment Samplingへ接続します。
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = m_Image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                1, &barrier);
            VkBufferImageCopy copy{};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = {width, height, 1};
            vkCmdCopyBufferToImage(command, staging, m_Image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                0, nullptr, 1, &barrier);
            if (vkEndCommandBuffer(command) != VK_SUCCESS)
            {
                break;
            }
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (vkCreateFence(m_Device, &fenceInfo, nullptr,
                &fence) != VK_SUCCESS)
            {
                break;
            }
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &command;
            if (vkQueueSubmit(device.GetGraphicsQueue(), 1, &submit,
                fence) != VK_SUCCESS)
            {
                break;
            }
            // Submit後はFence完了までStagingとCommandPoolを解放できません。
            if (vkWaitForFences(m_Device, 1, &fence, VK_TRUE,
                std::numeric_limits<uint64_t>::max()) != VK_SUCCESS)
            {
                // 待機失敗時もGPU参照が残り得るため、破棄前にQueue完了を待ちます。
                vkQueueWaitIdle(device.GetGraphicsQueue());
                break;
            }
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = m_Image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = image.format;
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.levelCount = 1;
            view.subresourceRange.layerCount = 1;
            if (vkCreateImageView(m_Device, &view, nullptr,
                &m_View) != VK_SUCCESS)
            {
                break;
            }
            VkSamplerCreateInfo sampler{};
            sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler.magFilter = VK_FILTER_LINEAR;
            sampler.minFilter = VK_FILTER_LINEAR;
            sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler.maxLod = 0.0f;
            if (vkCreateSampler(m_Device, &sampler, nullptr,
                &m_Sampler) != VK_SUCCESS)
            {
                break;
            }
            success = true;
        } while (false);

        if (fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_Device, fence, nullptr);
        }
        if (pool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(m_Device, pool, nullptr);
        }
        if (staging != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_Device, staging, nullptr);
        }
        if (stagingMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_Device, stagingMemory, nullptr);
        }
        if (success == false)
        {
            Shutdown();
        }
        return success;
    }

    void Shutdown()
    {
        if (m_Device != VK_NULL_HANDLE)
        {
            if (m_Sampler != VK_NULL_HANDLE)
            {
                vkDestroySampler(m_Device, m_Sampler, nullptr);
            }
            if (m_View != VK_NULL_HANDLE)
            {
                vkDestroyImageView(m_Device, m_View, nullptr);
            }
            if (m_Image != VK_NULL_HANDLE)
            {
                vkDestroyImage(m_Device, m_Image, nullptr);
            }
            if (m_Memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_Device, m_Memory, nullptr);
            }
        }
        m_Sampler = VK_NULL_HANDLE;
        m_View = VK_NULL_HANDLE;
        m_Image = VK_NULL_HANDLE;
        m_Memory = VK_NULL_HANDLE;
        m_Device = VK_NULL_HANDLE;
    }

    bool IsValid() const
    {
        return m_Image != VK_NULL_HANDLE && m_View != VK_NULL_HANDLE &&
            m_Sampler != VK_NULL_HANDLE;
    }
    VkImageView GetView() const { return m_View; }
    VkSampler GetSampler() const { return m_Sampler; }

private:
    static uint32_t FindMemoryType(const VulkanDevice& device,
        const VkMemoryRequirements& requirements,
        VkMemoryPropertyFlags flags)
    {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(
            device.GetPhysicalDeviceHandle(), &properties);
        for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            if ((requirements.memoryTypeBits & (1u << index)) != 0 &&
                (properties.memoryTypes[index].propertyFlags & flags) == flags)
            {
                return index;
            }
        }
        return UINT32_MAX;
    }

    VkDevice m_Device = VK_NULL_HANDLE;
    VkImage m_Image = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    VkImageView m_View = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
};

} // namespace Raven
