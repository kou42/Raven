#pragma once

#include "Raven/Renderer/RHI/RHIOffscreenRenderTarget.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>

namespace Raven
{

class DX12SceneRHITexture;

// DX12 Scene Contextと同じDevice／Attachment形式を使うoff-screen描画先です。
// Color ResourceはRTV書き込みとRaven UIのSRV Samplingで共有します。
class DX12OffscreenRenderTarget final : public RHIOffscreenRenderTarget
{
public:
    static constexpr std::uint32_t PickingReadbackSlotCount = 2u;

    bool Init(
        ID3D12Device* device,
        const RHIOffscreenRenderTargetSpecification& specification);

    const RHIOffscreenRenderTargetSpecification& GetSpecification() const override;
    const Ref<RHITexture>& GetColorAttachment() const override;
    bool IsValid() const override;
    bool SupportsEntityPicking() const override;
    bool RequestEntityPicking(std::uint32_t x, std::uint32_t y,
        std::uint64_t& outSequence) override;
    bool ConsumeEntityPickingResult(
        std::uint64_t& outSequence, std::int32_t& outEntityID) override;

    bool TakePendingEntityPickingRequest(RHIEntityPickingRequest& outRequest);
    bool ResolveEntityPickingReadback(
        std::uint32_t slot, std::uint64_t sequence);

    ID3D12Device* GetOwnerDevice() const;
    ID3D12Resource* GetColorResource() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const;
    ID3D12Resource* GetPickingResource() const;
    ID3D12Resource* GetPickingReadback(std::uint32_t slot) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetPickingRTV() const;

private:
    ID3D12Device* m_Device = nullptr;
    RHIOffscreenRenderTargetSpecification m_Specification{};
    Ref<RHITexture> m_ColorAttachment;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_PickingResource;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
        PickingReadbackSlotCount> m_PickingReadbacks;
    RHIEntityPickingReadbackState m_PickingState;
    UINT m_RtvDescriptorSize = 0u;
};

} // namespace Raven
