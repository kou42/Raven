#pragma once

#include "Raven/Renderer/RHI/RHIDevice.h"

namespace Raven
{

class OpenGLRHIDevice final : public RHIDevice
{
public:
    ~OpenGLRHIDevice() override = default;

    RHIBackend GetBackend() const override;
    RHIComputeCapabilities QueryComputeCapabilities() const override;
    Ref<RHIComputeCommandEncoder> CreateComputeCommandEncoder() override;

    Ref<RHIBuffer> CreateBuffer(
        const RHIBufferSpecification& specification,
        const void* initialData = nullptr) override;

    Ref<RHITexture> CreateTexture(
        const RHITextureSpecification& specification,
        const void* initialData = nullptr,
        std::size_t initialDataSize = 0) override;
};

} // namespace Raven
