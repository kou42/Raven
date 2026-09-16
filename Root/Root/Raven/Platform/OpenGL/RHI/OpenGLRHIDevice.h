#pragma once

#include "Raven/Renderer/RHI/RHIDevice.h"

namespace Raven
{

class OpenGLRHIDevice final : public RHIDevice
{
public:
    ~OpenGLRHIDevice() override = default;

    RHIBackend GetBackend() const override;

    Ref<RHIBuffer> CreateBuffer(
        const RHIBufferSpecification& specification,
        const void* initialData = nullptr) override;
};

} // namespace Raven
