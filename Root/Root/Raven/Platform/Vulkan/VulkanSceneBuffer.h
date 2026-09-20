#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Raven
{
class VulkanDevice;

// Scene用の小規模な頂点/Indexデータを保持するHost Visible + Coherent Bufferです。
// GPU実行中のBufferをSetData/Shutdownしないことを呼び出し側が保証します。
class VulkanSceneBuffer
{
public:
    VulkanSceneBuffer() = default;
    ~VulkanSceneBuffer();

    VulkanSceneBuffer(const VulkanSceneBuffer&) = delete;
    VulkanSceneBuffer& operator=(const VulkanSceneBuffer&) = delete;

    bool InitVertex(const VulkanDevice& device, const void* data,
        uint32_t byteSize, uint32_t stride);
    bool InitIndex(const VulkanDevice& device, const uint32_t* indices, uint32_t count);
    // RHIBuffer::SetDataと同じく部分更新を許可します。GPU利用中の同期は呼び出し側の責務です。
    bool SetData(const void* data, uint32_t byteSize, uint32_t offset = 0);
    // 容量変更時はnative Bufferを再生成します。GPU使用完了後に呼んでください。
    // data == nullptrの場合、新しい内容は未定義です。失敗時は旧Bufferを保持します。
    bool Resize(uint32_t byteSize, const void* data = nullptr);
    void Shutdown();

    bool IsValid() const { return m_Buffer != VK_NULL_HANDLE && m_Memory != VK_NULL_HANDLE; }
    bool IsIndexBuffer() const { return m_IsIndexBuffer; }
    VkBuffer GetHandle() const { return m_Buffer; }
    VkDevice GetDeviceHandle() const { return m_Device; }
    uint32_t GetIndexCount() const { return m_IndexCount; }
    uint32_t GetCapacity() const { return m_Capacity; }
    uint32_t GetVertexStride() const { return m_VertexStride; }

private:
    bool Init(const VulkanDevice& device, const void* data,
        uint32_t byteSize, uint32_t stride, bool indexBuffer, uint32_t indexCount);

    bool InitNative(VkDevice device, VkPhysicalDevice physicalDevice,
        const void* data, uint32_t byteSize, uint32_t stride,
        bool indexBuffer, uint32_t indexCount);

    VkDevice m_Device = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkBuffer m_Buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    uint32_t m_Capacity = 0;
    uint32_t m_VertexStride = 0;
    uint32_t m_IndexCount = 0;
    bool m_IsIndexBuffer = false;
};
} // namespace Raven
