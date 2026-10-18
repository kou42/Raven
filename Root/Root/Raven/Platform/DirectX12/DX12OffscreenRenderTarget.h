#pragma once

#include "Raven/Renderer/RHI/RHIOffscreenRenderTarget.h"

#include <d3d12.h>
#include <wrl/client.h>

namespace Raven
{

class DX12SceneRHITexture;

// DX12 Scene Contextと同じDevice／Attachment形式を使うoff-screen描画先です。
// Color ResourceはRTV書き込みとRaven UIのSRV Samplingで共有します。
class DX12OffscreenRenderTarget final : public RHIOffscreenRenderTarget
{
public:
    bool Init(
        ID3D12Device* device,
        const RHIOffscreenRenderTargetSpecification& specification);

    const RHIOffscreenRenderTargetSpecification& GetSpecification() const override;
    const Ref<RHITexture>& GetColorAttachment() const override;
    bool IsValid() const override;

    ID3D12Device* GetOwnerDevice() const;
    ID3D12Resource* GetColorResource() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const;

private:
    ID3D12Device* m_Device = nullptr;
    RHIOffscreenRenderTargetSpecification m_Specification{};
    Ref<RHITexture> m_ColorAttachment;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthResource;
};

} // namespace Raven
