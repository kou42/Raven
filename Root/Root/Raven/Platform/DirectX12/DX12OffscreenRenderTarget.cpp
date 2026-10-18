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
    if (color->InitRenderTarget(m_Device, colorSpecification) == false)
    {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvDescription{};
    rtvDescription.NumDescriptors = 1;
    rtvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    if (FAILED(m_Device->CreateDescriptorHeap(
        &rtvDescription, IID_PPV_ARGS(rtvHeap.GetAddressOf()))))
    {
        return false;
    }
    m_Device->CreateRenderTargetView(color->GetNativeTexture(), nullptr,
        rtvHeap->GetCPUDescriptorHandleForHeapStart());

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
            (m_DsvHeap != nullptr && m_DepthResource != nullptr));
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

} // namespace Raven
