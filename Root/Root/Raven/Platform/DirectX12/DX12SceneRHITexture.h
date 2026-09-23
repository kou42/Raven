#pragma once

#include "Raven/Renderer/RHI/RHITexture.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace Raven
{

// 初期RGBA8データをDEFAULT Heapへ転送するScene用Texture。
// 初期化時に専用Copy CommandのFence完了を待つため、Staging Bufferを安全に破棄できます。
// 頻繁なTexture生成向けの転送Queue共用は後続の最適化で扱います。
class DX12SceneRHITexture final : public RHITexture
{
public:
    bool Init(ID3D12Device* device,
        const RHITextureSpecification& specification,
        const void* initialData, std::size_t initialDataSize)
    {
        if (device == nullptr || initialData == nullptr ||
            specification.Format != RHITextureFormat::RGBA8 ||
            specification.Usage != RHITextureUsage::Sampled ||
            specification.GenerateMips == true ||
            specification.Width == 0 || specification.Height == 0 ||
            static_cast<uint64_t>(specification.Width) * specification.Height >
                std::numeric_limits<std::size_t>::max() / 4 ||
            initialDataSize != static_cast<std::size_t>(specification.Width) *
                specification.Height * 4)
        {
            return false;
        }

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC textureDesc{};
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDesc.Width = specification.Width;
        textureDesc.Height = specification.Height;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        if (FAILED(device->CreateCommittedResource(&defaultHeap,
            D3D12_HEAP_FLAG_NONE, &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(texture.GetAddressOf()))))
        {
            return false;
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows = 0;
        UINT64 rowBytes = 0;
        UINT64 uploadSize = 0;
        device->GetCopyableFootprints(&textureDesc, 0, 1, 0,
            &footprint, &rows, &rowBytes, &uploadSize);
        if (rows != specification.Height ||
            rowBytes != static_cast<UINT64>(specification.Width) * 4 ||
            uploadSize == 0)
        {
            return false;
        }

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC uploadDesc{};
        uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Width = uploadSize;
        uploadDesc.Height = 1;
        uploadDesc.DepthOrArraySize = 1;
        uploadDesc.MipLevels = 1;
        uploadDesc.SampleDesc.Count = 1;
        uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> upload;
        if (FAILED(device->CreateCommittedResource(&uploadHeap,
            D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(upload.GetAddressOf()))))
        {
            return false;
        }

        uint8_t* mapped = nullptr;
        D3D12_RANGE readRange{ 0, 0 };
        if (FAILED(upload->Map(0, &readRange,
            reinterpret_cast<void**>(&mapped))) || mapped == nullptr)
        {
            return false;
        }
        const uint8_t* source = static_cast<const uint8_t*>(initialData);
        for (UINT row = 0; row < rows; ++row)
        {
            std::memcpy(mapped + footprint.Offset +
                static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                source + static_cast<std::size_t>(row) * specification.Width * 4,
                static_cast<std::size_t>(rowBytes));
        }
        D3D12_RANGE writtenRange{ 0, static_cast<SIZE_T>(uploadSize) };
        upload->Unmap(0, &writtenRange);

        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&queueDesc,
            IID_PPV_ARGS(queue.GetAddressOf()))) ||
            FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(allocator.GetAddressOf()))) ||
            FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(), nullptr, IID_PPV_ARGS(list.GetAddressOf()))) ||
            FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(fence.GetAddressOf()))))
        {
            return false;
        }

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
        sourceLocation.pResource = upload.Get();
        sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        sourceLocation.PlacedFootprint = footprint;
        list->CopyTextureRegion(&destination, 0, 0, 0,
            &sourceLocation, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1, &barrier);
        if (FAILED(list->Close()))
        {
            return false;
        }
        ID3D12CommandList* commands[] = { list.Get() };
        queue->ExecuteCommandLists(1, commands);
        if (FAILED(queue->Signal(fence.Get(), 1)))
        {
            return false;
        }
        if (fence->GetCompletedValue() < 1)
        {
            HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (eventHandle == nullptr)
            {
                // Queue/UploadをGPU実行中に解放しないため、Event生成失敗時も待機します。
                while (fence->GetCompletedValue() < 1)
                {
                    Sleep(1);
                }
            }
            else
            {
                const HRESULT eventResult = fence->SetEventOnCompletion(1, eventHandle);
                if (SUCCEEDED(eventResult))
                {
                    WaitForSingleObject(eventHandle, INFINITE);
                }
                else
                {
                    while (fence->GetCompletedValue() < 1)
                    {
                        Sleep(1);
                    }
                }
                CloseHandle(eventHandle);
            }
        }

        // TextureごとにShader-visible SRVを1個所有します。
        // Descriptor TableのGPU HandleをFrame中に参照するためHeapもTextureと同寿命です。
        D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
        heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDescription.NumDescriptors = 1;
        heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap;
        if (FAILED(device->CreateDescriptorHeap(&heapDescription,
            IID_PPV_ARGS(srvHeap.GetAddressOf()))))
        {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(texture.Get(), &srv,
            srvHeap->GetCPUDescriptorHandleForHeapStart());

        m_SrvHeap = std::move(srvHeap);
        m_Texture = std::move(texture);
        m_Specification = specification;
        m_OwnerDevice = device;
        return true;
    }

    void SetData(const void* data, std::size_t dataSize) override
    {
        (void)data;
        (void)dataSize;
        // 描画中の更新はFence管理を伴う別経路が必要なため初期転送のみ対応します。
    }

    const RHITextureSpecification& GetSpecification() const override
    {
        return m_Specification;
    }

    ID3D12Resource* GetNativeTexture() const { return m_Texture.Get(); }
    ID3D12DescriptorHeap* GetSrvHeap() const { return m_SrvHeap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle() const
    {
        return m_SrvHeap != nullptr ?
            m_SrvHeap->GetGPUDescriptorHandleForHeapStart() :
            D3D12_GPU_DESCRIPTOR_HANDLE{};
    }
    ID3D12Device* GetOwnerDevice() const { return m_OwnerDevice; }

private:
    RHITextureSpecification m_Specification{};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_Texture;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
    ID3D12Device* m_OwnerDevice = nullptr;
};

} // namespace Raven
