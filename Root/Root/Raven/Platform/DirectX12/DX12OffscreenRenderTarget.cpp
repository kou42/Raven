#include "DX12OffscreenRenderTarget.h"

#include "DX12SceneRHITexture.h"

namespace Raven
{

bool DX12OffscreenRenderTarget::Init(
    ID3D12Device* device,
    const RHIOffscreenRenderTargetSpecification& specification)
{
    if (device == nullptr ||
        IsValidRHIOffscreenRenderTargetSpecification(specification) == false)
    {
        return false;
    }
    m_Device = device;
    RHITextureSpecification colorSpecification{};
    colorSpecification.Width = specification.Width;
    colorSpecification.Height = specification.Height;
    colorSpecification.Format = specification.ColorFormat;
    colorSpecification.Usage = RHITextureUsage::RenderTarget;
    colorSpecification.GenerateMips = false;
    colorSpecification.DebugName = specification.DebugName + " Color";
    auto color = CreateRef<DX12SceneRHITexture>();
    if (color->InitRenderTarget(
        m_Device, colorSpecification, specification.ClearColor) == false)
    {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvDescription{};
    rtvDescription.NumDescriptors = specification.EnableEntityPicking == true ? 2u : 1u;
    rtvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    if (FAILED(m_Device->CreateDescriptorHeap(
        &rtvDescription, IID_PPV_ARGS(rtvHeap.GetAddressOf()))))
    {
        return false;
    }
    m_Device->CreateRenderTargetView(color->GetNativeTexture(), nullptr,
        rtvHeap->GetCPUDescriptorHandleForHeapStart());

    Microsoft::WRL::ComPtr<ID3D12Resource> pickingResource;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
        PickingReadbackSlotCount> pickingReadbacks;
    const UINT rtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if (specification.EnableEntityPicking == true)
    {
        D3D12_RESOURCE_DESC pickingDescription{};
        pickingDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        pickingDescription.Width = specification.Width;
        pickingDescription.Height = specification.Height;
        pickingDescription.DepthOrArraySize = 1;
        pickingDescription.MipLevels = 1;
        pickingDescription.Format = DXGI_FORMAT_R32_SINT;
        pickingDescription.SampleDesc.Count = 1;
        pickingDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        pickingDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE pickingClear{};
        pickingClear.Format = DXGI_FORMAT_R32_SINT;
        pickingClear.Color[0] = -1.0f;
        if (FAILED(m_Device->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &pickingDescription,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &pickingClear,
            IID_PPV_ARGS(pickingResource.GetAddressOf()))))
        {
            return false;
        }

        D3D12_RENDER_TARGET_VIEW_DESC pickingView{};
        pickingView.Format = DXGI_FORMAT_R32_SINT;
        pickingView.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE pickingRtv =
            rtvHeap->GetCPUDescriptorHandleForHeapStart();
        pickingRtv.ptr += rtvDescriptorSize;
        m_Device->CreateRenderTargetView(
            pickingResource.Get(), &pickingView, pickingRtv);

        // Texture->Buffer copyのRowPitchは256 byte alignmentが必要です。
        // 1 pixelだけ読む場合も固定長BufferをFrame slotごとに分け、GPU実行中の上書きを防ぎます。
        D3D12_RESOURCE_DESC readbackDescription{};
        readbackDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readbackDescription.Width = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        readbackDescription.Height = 1;
        readbackDescription.DepthOrArraySize = 1;
        readbackDescription.MipLevels = 1;
        readbackDescription.SampleDesc.Count = 1;
        readbackDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
        for (auto& readback : pickingReadbacks)
        {
            if (FAILED(m_Device->CreateCommittedResource(
                &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDescription,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(readback.GetAddressOf()))))
            {
                return false;
            }
        }
    }

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthResource;
    if (specification.EnableDepth == true)
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvDescription{};
        dsvDescription.NumDescriptors = 1;
        dsvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        if (FAILED(m_Device->CreateDescriptorHeap(
            &dsvDescription, IID_PPV_ARGS(dsvHeap.GetAddressOf()))))
        {
            return false;
        }

        D3D12_RESOURCE_DESC depthDescription{};
        depthDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDescription.Width = specification.Width;
        depthDescription.Height = specification.Height;
        depthDescription.DepthOrArraySize = 1;
        depthDescription.MipLevels = 1;
        depthDescription.Format = DXGI_FORMAT_D32_FLOAT;
        depthDescription.SampleDesc.Count = 1;
        depthDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = depthDescription.Format;
        clearValue.DepthStencil.Depth = 1.0f;
        if (FAILED(m_Device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &depthDescription,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
            IID_PPV_ARGS(depthResource.GetAddressOf()))))
        {
            return false;
        }
        m_Device->CreateDepthStencilView(
            depthResource.Get(), nullptr,
            dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // 全Resource生成成功後にまとめて設定し、部分初期化Targetを公開しません。
    m_ColorAttachment = std::move(color);
    m_RtvHeap = std::move(rtvHeap);
    m_DsvHeap = std::move(dsvHeap);
    m_DepthResource = std::move(depthResource);
    m_PickingResource = std::move(pickingResource);
    m_PickingReadbacks = std::move(pickingReadbacks);
    m_RtvDescriptorSize = rtvDescriptorSize;
    m_PickingState.Reset();
    m_Specification = specification;
    return true;
}

const RHIOffscreenRenderTargetSpecification&
DX12OffscreenRenderTarget::GetSpecification() const
{
    return m_Specification;
}

const Ref<RHITexture>& DX12OffscreenRenderTarget::GetColorAttachment() const
{
    return m_ColorAttachment;
}

bool DX12OffscreenRenderTarget::IsValid() const
{
    return m_Device != nullptr && m_ColorAttachment != nullptr &&
        m_RtvHeap != nullptr &&
        (m_Specification.EnableDepth == false ||
            (m_DsvHeap != nullptr && m_DepthResource != nullptr)) &&
        (m_Specification.EnableEntityPicking == false ||
            (m_PickingResource != nullptr &&
                m_PickingReadbacks[0] != nullptr &&
                m_PickingReadbacks[1] != nullptr));
}

bool DX12OffscreenRenderTarget::SupportsEntityPicking() const
{
    return IsValid() == true && m_Specification.EnableEntityPicking == true;
}

bool DX12OffscreenRenderTarget::RequestEntityPicking(
    std::uint32_t x,
    std::uint32_t y,
    std::uint64_t& outSequence)
{
    if (SupportsEntityPicking() == false)
    {
        return false;
    }
    return m_PickingState.Request(
        m_Specification.Width, m_Specification.Height, x, y, outSequence);
}

bool DX12OffscreenRenderTarget::ConsumeEntityPickingResult(
    std::uint64_t& outSequence,
    std::int32_t& outEntityID)
{
    return m_PickingState.ConsumeResult(outSequence, outEntityID);
}

bool DX12OffscreenRenderTarget::TakePendingEntityPickingRequest(
    RHIEntityPickingRequest& outRequest)
{
    return SupportsEntityPicking() == true &&
        m_PickingState.TakePendingRequest(outRequest);
}

bool DX12OffscreenRenderTarget::ResolveEntityPickingReadback(
    std::uint32_t slot,
    std::uint64_t sequence)
{
    ID3D12Resource* readback = GetPickingReadback(slot);
    if (readback == nullptr || sequence == 0u)
    {
        return false;
    }
    D3D12_RANGE readRange{ 0u, sizeof(std::int32_t) };
    void* mapped = nullptr;
    if (FAILED(readback->Map(0u, &readRange, &mapped)) || mapped == nullptr)
    {
        return false;
    }
    const std::int32_t entityID = *static_cast<const std::int32_t*>(mapped);
    const D3D12_RANGE writtenRange{ 0u, 0u };
    readback->Unmap(0u, &writtenRange);
    return m_PickingState.PublishResult(sequence, entityID);
}

ID3D12Device* DX12OffscreenRenderTarget::GetOwnerDevice() const
{
    return m_Device;
}

ID3D12Resource* DX12OffscreenRenderTarget::GetColorResource() const
{
    const auto texture =
        std::dynamic_pointer_cast<DX12SceneRHITexture>(m_ColorAttachment);
    return texture != nullptr ? texture->GetNativeTexture() : nullptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12OffscreenRenderTarget::GetRTV() const
{
    return m_RtvHeap != nullptr
        ? m_RtvHeap->GetCPUDescriptorHandleForHeapStart()
        : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12OffscreenRenderTarget::GetDSV() const
{
    return m_DsvHeap != nullptr
        ? m_DsvHeap->GetCPUDescriptorHandleForHeapStart()
        : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

ID3D12Resource* DX12OffscreenRenderTarget::GetPickingResource() const
{
    return m_PickingResource.Get();
}

ID3D12Resource* DX12OffscreenRenderTarget::GetPickingReadback(
    std::uint32_t slot) const
{
    return slot < m_PickingReadbacks.size()
        ? m_PickingReadbacks[slot].Get()
        : nullptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12OffscreenRenderTarget::GetPickingRTV() const
{
    if (m_RtvHeap == nullptr || m_RtvDescriptorSize == 0u ||
        m_Specification.EnableEntityPicking == false)
    {
        return {};
    }
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += m_RtvDescriptorSize;
    return handle;
}

} // namespace Raven
