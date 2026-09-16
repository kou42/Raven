#pragma once

#include "Raven/Renderer/RHI/RHITexture.h"

namespace Raven
{

class OpenGLRHITexture final : public RHITexture
{
public:
    explicit OpenGLRHITexture(
        const RHITextureSpecification& specification,
        const void* initialData = nullptr,
        std::size_t initialDataSize = 0);
    ~OpenGLRHITexture() override;

    void SetData(const void* data, std::size_t dataSize) override;
    const RHITextureSpecification& GetSpecification() const override;

    // Legacy OpenGL Texture / FramebufferとのBridgeでのみ利用するnative handleです。
    // 上位RendererへGLuintを公開しないため、RHI共通interfaceには含めません。
    unsigned int GetRendererID() const;

private:
    void Invalidate(const void* initialData, std::size_t initialDataSize);
    static std::uint32_t GetBytesPerPixel(RHITextureFormat format);

private:
    unsigned int m_RendererID = 0;
    RHITextureSpecification m_Specification;
};

} // namespace Raven
