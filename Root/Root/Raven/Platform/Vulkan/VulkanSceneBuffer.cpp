#include "VulkanSceneBuffer.h"

#include "VulkanDevice.h"

#include <cstring>
#include <limits>
#include <utility>

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
    // 共通RHIDeviceではinitialData == nullptrの未初期化Bufferも生成します。
    if (device.IsValid() == false || byteSize == 0 || stride == 0)
    {
        return false;
    }
    return InitNative(device.GetHandle(), device.GetPhysicalDeviceHandle(),
        data, byteSize, stride, indexBuffer, indexCount);
}

bool VulkanSceneBuffer::InitNative(VkDevice device, VkPhysicalDevice physicalDevice,
    const void* data, uint32_t byteSize, uint32_t stride,
    bool indexBuffer, uint32_t indexCount)
{
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE ||
        byteSize == 0 || stride == 0)
    {
        return false;
    }
    m_Device = device;
    m_PhysicalDevice = physicalDevice;
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
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memoryProperties);

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
    if (data != nullptr && SetData(data, byteSize) == false)
    {
        Shutdown();
        return false;
    }
    return true;
}

bool VulkanSceneBuffer::Resize(uint32_t byteSize, const void* data)
{
    if (IsValid() == false || byteSize == 0)
    {
        return false;
    }
    const uint32_t stride = m_IsIndexBuffer == true ?
        static_cast<uint32_t>(sizeof(uint32_t)) : m_VertexStride;
    if (stride == 0 || byteSize % stride != 0)
    {
        return false;
    }
    if (byteSize == m_Capacity)
    {
        return data == nullptr || SetData(data, byteSize);
    }

    // 先に新Resourceを完成させ、失敗した場合は旧Bufferを維持します。
    // 旧Bufferの破棄はGPUの読み取り完了後でなければならないため、
    // 呼び出し元はResize前にFence/WaitIdle等で同期してください。
    VulkanSceneBuffer replacement;
    if (replacement.InitNative(m_Device, m_PhysicalDevice, data, byteSize,
        stride, m_IsIndexBuffer,
        m_IsIndexBuffer == true ? byteSize / sizeof(uint32_t) : 0) == false)
    {
        return false;
    }
    std::swap(m_Device, replacement.m_Device);
    std::swap(m_PhysicalDevice, replacement.m_PhysicalDevice);
    std::swap(m_Buffer, replacement.m_Buffer);
    std::swap(m_Memory, replacement.m_Memory);
    std::swap(m_Capacity, replacement.m_Capacity);
    std::swap(m_VertexStride, replacement.m_VertexStride);
    std::swap(m_IndexCount, replacement.m_IndexCount);
    std::swap(m_IsIndexBuffer, replacement.m_IsIndexBuffer);
    return true;
}

bool VulkanSceneBuffer::SetData(const void* data, uint32_t byteSize, uint32_t offset)
{
    // 減算で範囲検証し、offset + byteSizeの整数overflowを防ぎます。
    // GPU実行中の書き込みは呼び出し側がFence等で同期してください。
    if (IsValid() == false || data == nullptr || byteSize == 0 ||
        offset > m_Capacity || byteSize > m_Capacity - offset)
    {
        return false;
    }

    void* mapped = nullptr;
    // VkDeviceMemoryのMap offsetにはminMemoryMapAlignment等の制約があるため、
    // 割当先頭からMapし、CPU側pointerで更新位置を指定します。
    if (vkMapMemory(m_Device, m_Memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS ||
        mapped == nullptr)
    {
        return false;
    }
    std::memcpy(static_cast<uint8_t*>(mapped) + offset, data, byteSize);
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
    m_PhysicalDevice = VK_NULL_HANDLE;
    m_Buffer = VK_NULL_HANDLE;
    m_Memory = VK_NULL_HANDLE;
    m_Capacity = 0;
    m_VertexStride = 0;
    m_IndexCount = 0;
    m_IsIndexBuffer = false;
}
} // namespace Raven
