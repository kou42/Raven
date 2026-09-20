#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

namespace Raven
{
// Scene用の小規模な頂点/IndexデータをCPUから転送する学習用Upload Heap Bufferです。
// GPU実行中のBufferをSetDataで書き換えないことを呼び出し側が保証します。
class DX12SceneBuffer
{
public:
    DX12SceneBuffer() = default;
    ~DX12SceneBuffer();

    DX12SceneBuffer(const DX12SceneBuffer&) = delete;
    DX12SceneBuffer& operator=(const DX12SceneBuffer&) = delete;

    bool InitVertex(ID3D12Device* device, const void* data,
        uint32_t byteSize, uint32_t stride);
    bool InitIndex(ID3D12Device* device, const uint32_t* indices, uint32_t count);
    bool SetData(const void* data, uint32_t byteSize);
    void Shutdown();

    bool IsValid() const { return m_Buffer.Get() != nullptr; }
    bool IsIndexBuffer() const { return m_IsIndexBuffer; }
    uint32_t GetIndexCount() const { return m_IndexCount; }
    const D3D12_VERTEX_BUFFER_VIEW& GetVertexView() const { return m_VertexView; }
    const D3D12_INDEX_BUFFER_VIEW& GetIndexView() const { return m_IndexView; }

private:
    bool Init(ID3D12Device* device, const void* data, uint32_t byteSize,
        uint32_t stride, bool indexBuffer, uint32_t indexCount);

    Microsoft::WRL::ComPtr<ID3D12Resource> m_Buffer;
    D3D12_VERTEX_BUFFER_VIEW m_VertexView{};
    D3D12_INDEX_BUFFER_VIEW m_IndexView{};
    uint32_t m_Capacity = 0;
    uint32_t m_IndexCount = 0;
    bool m_IsIndexBuffer = false;
};
} // namespace Raven
