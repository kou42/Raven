#pragma once

#include "Raven/Renderer/RHI/RHIBuffer.h"
#include "VulkanDevice.h"
#include "VulkanSceneBuffer.h"

#include <cstdint>
#include <limits>

namespace Raven
{

// 共通RHIBufferとScene用VkBufferを接続するAdapterです。
// GPUが使用中のSetData/Resize/破棄は呼び出し元が同期してください。
// ContextのVkDeviceより先にこのResourceを破棄する必要があります。
class VulkanSceneRHIBuffer final : public RHIBuffer
{
public:
    bool Init(const VulkanDevice& device,
        const RHIBufferSpecification& specification, const void* initialData)
    {
        if (device.IsValid() == false || specification.Size == 0 ||
            specification.Size > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }

        const uint32_t byteSize = static_cast<uint32_t>(specification.Size);
        bool created = false;
        switch (specification.Usage)
        {
        case RHIBufferUsage::Vertex:
            // RHIBufferSpecificationにstrideはないため、ここではbyte単位で確保します。
            // Scene DrawIndexedへ接続する際はPipeline入力宣言のstrideを別途伝えます。
            created = m_Buffer.InitVertex(device, initialData, byteSize, 1);
            break;
        case RHIBufferUsage::Index:
            if (byteSize % sizeof(uint32_t) != 0)
            {
                return false;
            }
            created = m_Buffer.InitIndex(device,
                static_cast<const uint32_t*>(initialData),
                byteSize / static_cast<uint32_t>(sizeof(uint32_t)));
            break;
        case RHIBufferUsage::None:
        case RHIBufferUsage::Uniform:
        case RHIBufferUsage::Storage:
        default:
            // native Scene Bufferが対応する用途だけを成功扱いします。
            return false;
        }

        if (created == false)
        {
            return false;
        }
        m_Specification = specification;
        return true;
    }

    void SetData(const void* data, std::size_t size,
        std::size_t offset = 0) override
    {
        if (size == 0 || size > std::numeric_limits<uint32_t>::max() ||
            offset > m_Specification.Size ||
            size > m_Specification.Size - offset)
        {
            return;
        }
        m_Buffer.SetData(data, static_cast<uint32_t>(size),
            static_cast<uint32_t>(offset));
    }

    void Resize(std::size_t size, const void* data = nullptr) override
    {
        if (size == 0 || size > std::numeric_limits<uint32_t>::max())
        {
            return;
        }
        // 失敗時はnative Bufferと共通Specificationの両方を変更しません。
        if (m_Buffer.Resize(static_cast<uint32_t>(size), data) == true)
        {
            m_Specification.Size = size;
        }
    }

    const RHIBufferSpecification& GetSpecification() const override
    {
        return m_Specification;
    }

    // Scene固有の描画経路だけがnative Bufferを取得します。
    const VulkanSceneBuffer& GetSceneBuffer() const { return m_Buffer; }

private:
    RHIBufferSpecification m_Specification{};
    VulkanSceneBuffer m_Buffer;
};

} // namespace Raven
