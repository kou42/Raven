#pragma once

#include "Raven/Renderer/RHI/RHIBuffer.h"
#include "VulkanSceneContext.h"
#include "VulkanSceneBuffer.h"

#include <cstdint>
#include <limits>

namespace Raven
{

// 共通RHIBufferとScene用VkBufferを接続するAdapterです。
// SetData/ResizeはContextでGPU同期し、Context終了時はnative Bufferを無効化します。
// Draw記録済みBufferはContextが対応Frame Fence完了まで保持します。
class VulkanSceneRHIBuffer final : public RHIBuffer
{
public:
    ~VulkanSceneRHIBuffer() override
    {
        Shutdown();
    }

    bool Init(VulkanSceneContext& context,
        const RHIBufferSpecification& specification, const void* initialData)
    {
        if (context.GetDevice().IsValid() == false || specification.Size == 0 ||
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
            // DrawIndexedはBind済PipelineのBinding 0からstrideを取得します。
            created = m_Buffer.InitVertex(context.GetDevice(), initialData, byteSize, 1);
            break;
        case RHIBufferUsage::Index:
            if (byteSize % sizeof(uint32_t) != 0)
            {
                return false;
            }
            created = m_Buffer.InitIndex(context.GetDevice(),
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
        m_Context = &context;
        return true;
    }

    // GPU同期を含む更新結果を共通RHIBuffer APIへ返します。
    bool TrySetData(
        const void* data, std::size_t size, std::size_t offset = 0) override
    {
        if (m_Context == nullptr || data == nullptr ||
            size == 0 || size > std::numeric_limits<uint32_t>::max() ||
            offset > m_Specification.Size ||
            size > m_Specification.Size - offset ||
            m_Context->SynchronizeBufferAccess(*this) == false)
        {
            return false;
        }
        return m_Buffer.SetData(data, static_cast<uint32_t>(size),
            static_cast<uint32_t>(offset));
    }

    bool TryResize(std::size_t size, const void* data = nullptr) override
    {
        if (m_Context == nullptr || size == 0 ||
            size > std::numeric_limits<uint32_t>::max() ||
            m_Context->SynchronizeBufferAccess(*this) == false)
        {
            return false;
        }
        // native Bufferの再生成に失敗した場合、Specificationも旧値を保持します。
        if (m_Buffer.Resize(static_cast<uint32_t>(size), data) == false)
        {
            return false;
        }
        m_Specification.Size = size;
        return true;
    }

    // Context::ShutdownはWaitIdle後に呼び、外部RefからのDevice破棄後アクセスを防ぎます。
    void InvalidateAfterDeviceIdle()
    {
        m_Buffer.Shutdown();
        m_Context = nullptr;
    }

    const RHIBufferSpecification& GetSpecification() const override
    {
        return m_Specification;
    }

    // Scene固有の描画経路だけがnative Bufferを取得します。
    const VulkanSceneBuffer& GetSceneBuffer() const { return m_Buffer; }

private:
    void Shutdown()
    {
        // 描画済みBufferはContextがFrame Fence完了まで強参照するため、
        // 最後のRefの破棄時に再入するFence待機は不要です。
        m_Buffer.Shutdown();
        m_Context = nullptr;
    }

    VulkanSceneContext* m_Context = nullptr;
    RHIBufferSpecification m_Specification{};
    VulkanSceneBuffer m_Buffer;
};

} // namespace Raven
