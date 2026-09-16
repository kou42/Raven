#pragma once

#include <cstddef>

#include "Raven/Core/Base.h"
#include "Raven/Renderer/RHI/RHIBuffer.h"
#include "Raven/Renderer/RHI/RHITexture.h"
#include "Raven/Renderer/RHI/RHITypes.h"

namespace Raven
{

// ============================================================================
// RHIDevice
// ============================================================================
// GPU Resource生成の入口をGraphics APIから分離します。
// 将来Shader / Pipeline等もこのDeviceへ集約し、上位RendererがOpenGL実装を
// 直接生成しない構造へ段階的に移行します。
class RHIDevice
{
public:
    virtual ~RHIDevice() = default;

    virtual RHIBackend GetBackend() const = 0;

    virtual Ref<RHIBuffer> CreateBuffer(
        const RHIBufferSpecification& specification,
        const void* initialData = nullptr) = 0;

    virtual Ref<RHITexture> CreateTexture(
        const RHITextureSpecification& specification,
        const void* initialData = nullptr,
        std::size_t initialDataSize = 0) = 0;
};

} // namespace Raven
