#include "DX12SceneBuffer.h"

#include <cstring>
#include <limits>

namespace Raven
{
DX12SceneBuffer::~DX12SceneBuffer()
{
    Shutdown();
}

bool DX12SceneBuffer::InitVertex(ID3D12Device* device, const void* data,
    uint32_t byteSize, uint32_t stride)
{
    if (stride == 0 || byteSize % stride != 0)
    {
        return false;
    }
    return Init(device, data, byteSize, stride, false, 0);
}

bool DX12SceneBuffer::InitIndex(ID3D12Device* device,
    const uint32_t* indices, uint32_t count)
{
    if (count == 0 || count > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t))
    {
        return false;
    }
    return Init(device, indices, count * sizeof(uint32_t),
        sizeof(uint32_t), true, count);
}

bool DX12SceneBuffer::Init(ID3D12Device* device, const void* data,
    uint32_t byteSize, uint32_t stride, bool indexBuffer, uint32_t indexCount)
{
    Shutdown();
    if (device == nullptr || data == nullptr || byteSize == 0 || stride == 0)
    {
        return false;
    }

    // 最初の段階では転送用CommandListを必要としないUpload Heapを採用します。
    // 頻繁に描画するMeshは後続のDefault Heap + Staging転送へ移行します。
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = byteSize;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(m_Buffer.ReleaseAndGetAddressOf()))))
    {
        Shutdown();
        return false;
    }

    m_Capacity = byteSize;
    m_IsIndexBuffer = indexBuffer;
    m_IndexCount = indexCount;
    if (SetData(data, byteSize) == false)
    {
        Shutdown();
        return false;
    }

    if (indexBuffer == true)
    {
        m_IndexView.BufferLocation = m_Buffer->GetGPUVirtualAddress();
        m_IndexView.SizeInBytes = byteSize;
        m_IndexView.Format = DXGI_FORMAT_R32_UINT;
    }
    else
    {
        m_VertexView.BufferLocation = m_Buffer->GetGPUVirtualAddress();
        m_VertexView.SizeInBytes = byteSize;
        m_VertexView.StrideInBytes = stride;
    }
    return true;
}

bool DX12SceneBuffer::SetData(const void* data, uint32_t byteSize)
{
    if (m_Buffer.Get() == nullptr || data == nullptr ||
        byteSize == 0 || byteSize > m_Capacity)
    {
        return false;
    }

    void* mapped = nullptr;
    const D3D12_RANGE readRange{ 0, 0 };
    if (FAILED(m_Buffer->Map(0, &readRange, &mapped)) || mapped == nullptr)
    {
        return false;
    }
    std::memcpy(mapped, data, byteSize);
    const D3D12_RANGE writtenRange{ 0, byteSize };
    m_Buffer->Unmap(0, &writtenRange);
    // Buffer Viewのサイズは初期容量のままです。部分更新では残りの領域を描画しません。
    return true;
}

void DX12SceneBuffer::Shutdown()
{
    m_Buffer.Reset();
    m_VertexView = {};
    m_IndexView = {};
    m_Capacity = 0;
    m_IndexCount = 0;
    m_IsIndexBuffer = false;
}
} // namespace Raven
