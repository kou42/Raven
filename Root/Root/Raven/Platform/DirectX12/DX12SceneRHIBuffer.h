#pragma once

#include "DX12SceneBuffer.h"
#include "Raven/Renderer/RHI/RHIBuffer.h"

#include <cstdint>
#include <limits>

namespace Raven
{

// Scene Contextと同じDeviceで確保するDX12 Vertex/Index BufferのRHI Adapterです。
// Upload HeapのGPU参照中にCPUから更新する同期機構は未実装のため、現段階では
// 初期Uploadのみを許可します。Dynamic更新はFence同期を導入してから有効化します。
class DX12SceneRHIBuffer final : public RHIBuffer
{
public:
    bool Init(ID3D12Device* device,
        const RHIBufferSpecification& specification, const void* initialData)
    {
        if (device == nullptr || initialData == nullptr ||
            specification.Size == 0 ||
            specification.Size > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }

        const uint32_t byteSize = static_cast<uint32_t>(specification.Size);
        bool created = false;
        switch (specification.Usage)
        {
        case RHIBufferUsage::Vertex:
            // Vertex strideはPipelineのInput Layout側で定義します。
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
            return false;
        }

        if (created == false)
        {
            return false;
        }

        m_Specification = specification;
        m_OwnerDevice = device;
        return true;
    }

    bool TrySetData(
        const void* data, std::size_t size, std::size_t offset = 0) override
    {
        (void)data;
        (void)size;
        (void)offset;
        // GPU実行中のUpload Heap書換えを避けるため、Fence同期実装までは拒否します。
        return false;
    }

    bool TryResize(std::size_t size, const void* data = nullptr) override
    {
        (void)size;
        (void)data;
        return false;
    }

    const RHIBufferSpecification& GetSpecification() const override
    {
        return m_Specification;
    }

    // 同じBackendでも別DeviceのGPU仮想Addressは混在させられません。
    ID3D12Device* GetOwnerDevice() const
    {
        return m_OwnerDevice;
    }

    const DX12SceneBuffer& GetSceneBuffer() const
    {
        return m_Buffer;
    }

private:
    RHIBufferSpecification m_Specification{};
    ID3D12Device* m_OwnerDevice = nullptr;
    DX12SceneBuffer m_Buffer;
};

} // namespace Raven
