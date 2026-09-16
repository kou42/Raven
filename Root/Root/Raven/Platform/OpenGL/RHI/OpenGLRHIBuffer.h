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

    void SetData(const void* data, std::size_t size, std::size_t offset = 0) override;
    const RHIBufferSpecification& GetSpecification() const override;

    // OpenGL互換層向けの容量変更です。
    // VAOはVertex/Index BufferのOpenGL object名を保持するため、Legacy Bufferの容量拡張時も
    // object自体を作り直さずglBufferDataで同じhandleのstorageだけを再確保します。
    void Resize(std::size_t size, const void* data = nullptr);

    // native handleはOpenGL Backend内部でCommandList等が利用するための値です。
    // Renderer共通層からこの型を参照しないことを前提とします。
    std::uint32_t GetRendererID() const;

private:
    RHIBufferSpecification m_Specification{};
    std::uint32_t m_RendererID = 0;
};

} // namespace Raven
