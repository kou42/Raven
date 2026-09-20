#pragma once

#include <cstdint>

#include "Raven/Renderer/RHI/RHIBuffer.h"

namespace Raven
{

class OpenGLRHIBuffer final : public RHIBuffer
{
public:
    OpenGLRHIBuffer(const RHIBufferSpecification& specification, const void* initialData);
    ~OpenGLRHIBuffer() override;

    bool TrySetData(
        const void* data, std::size_t size, std::size_t offset = 0) override;
    const RHIBufferSpecification& GetSpecification() const override;

    // VAOはVertex/Index BufferのOpenGL object名を保持するため、容量拡張時も
    // object自体を作り直さずglBufferDataで同じhandleのstorageだけを再確保します。
    bool TryResize(std::size_t size, const void* data = nullptr) override;

    // native handleはOpenGL Backend内部でCommandList等が利用するための値です。
    // Renderer共通層からこの型を参照しないことを前提とします。
    std::uint32_t GetRendererID() const;

private:
    RHIBufferSpecification m_Specification{};
    std::uint32_t m_RendererID = 0;
};

} // namespace Raven
