#include "VulkanSceneBuffer.h"

#include "VulkanDevice.h"

#include <cstring>
#include <limits>

namespace Raven
{
VulkanSceneBuffer::~VulkanSceneBuffer()
{
    Shutdown();
}

bool VulkanSceneBuffer::InitVertex(const VulkanDevice& device, const void* data,
    uint32_t byteSize, uint32_t stride)
{
    if (stride == 0 || byteSize % stride != 0)
    {
        return false;
    }
    return Init(device, data, byteSize, stride, false, 0);
}

bool VulkanSceneBuffer::InitIndex(const VulkanDevice& device,
    const uint32_t* indices, uint32_t count)
{
    if (count == 0 || count > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t))
    {
        return false;
    }
    return Init(device, indices, count * sizeof(uint32_t),
        sizeof(uint32_t), true, count);
}

bool VulkanSceneBuffer::Init(const VulkanDevice& device, const void* data,
    uint32_t byteSize, uint32_t stride, bool indexBuffer, uint32_t indexCount)
{
    Shutdown();
    if (device.IsValid() == false || data == nullptr ||
        byteSize == 0 || stride == 0)
    {
        return false;
    }

    m_Device = device.GetHandle();
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = byteSize;
    bufferInfo.usage = indexBuffer == true ?
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &m_Buffer) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_Device, m_Buffer, &requirements);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(device.GetPhysicalDeviceHandle(), &memoryProperties);

    // Host CoherentなMemory Typeのみ採用し、SetDataの明示Flushを不要にします。
    // 該当TypeがないGPUでは失敗を返し、後続のStaging転送実装で対応します。
    uint32_t memoryTypeIndex = UINT32_MAX;
    const VkMemoryPropertyFlags requiredFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index)
    {
        if ((requirements.memoryTypeBits & (1u << index)) != 0 &&
            (memoryProperties.memoryTypes[index].propertyFlags & requiredFlags) == requiredFlags)
        {
            memoryTypeIndex = index;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX)
    {
        Shutdown();
        return false;
    }

    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryTypeIndex;
    if (vkAllocateMemory(m_Device, &allocation, nullptr, &m_Memory) != VK_SUCCESS ||
        vkBindBufferMemory(m_Device, m_Buffer, m_Memory, 0) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    m_Capacity = byteSize;
    m_VertexStride = indexBuffer == true ? 0 : stride;
    m_IndexCount = indexCount;
    m_IsIndexBuffer = indexBuffer;
    if (SetData(data, byteSize) == false)
    {
        Shutdown();
        return false;
    }
    return true;
}

bool VulkanSceneBuffer::SetData(const void* data, uint32_t byteSize)
{
    if (IsValid() == false || data == nullptr ||
        byteSize == 0 || byteSize > m_Capacity)
    {
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(m_Device, m_Memory, 0, byteSize, 0, &mapped) != VK_SUCCESS ||
        mapped == nullptr)
    {
        return false;
    }
    std::memcpy(mapped, data, byteSize);
    // Host Coherentを要求しているため、vkFlushMappedMemoryRangesは不要です。
    vkUnmapMemory(m_Device, m_Memory);
    return true;
}

void VulkanSceneBuffer::Shutdown()
{
    if (m_Device != VK_NULL_HANDLE)
    {
        // GPUの読み取り完了を呼び出し元が保証してから破棄します。
        if (m_Buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_Device, m_Buffer, nullptr);
        }
        if (m_Memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_Device, m_Memory, nullptr);
        }
    }
    m_Device = VK_NULL_HANDLE;
    m_Buffer = VK_NULL_HANDLE;
    m_Memory = VK_NULL_HANDLE;
    m_Capacity = 0;
    m_VertexStride = 0;
    m_IndexCount = 0;
    m_IsIndexBuffer = false;
}
} // namespace Raven
