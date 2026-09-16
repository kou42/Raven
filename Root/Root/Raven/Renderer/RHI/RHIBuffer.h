#pragma once

#include <cstddef>

#include "Raven/Renderer/RHI/RHIResource.h"
#include "Raven/Renderer/RHI/RHITypes.h"

namespace Raven
{

// ============================================================================
// RHIBuffer
// ============================================================================
// Vertex / Index / Uniform等のGPU Bufferを共通Resourceとして扱う低レベル抽象です。
// Vertex layoutのような描画入力定義はBuffer自体へ持たせず、将来のPipeline/Input Layout側で扱います。
class RHIBuffer : public RHIResource
{
public:
    ~RHIBuffer() override = default;

    virtual void SetData(const void* data, std::size_t size, std::size_t offset = 0) = 0;

    // Dynamic Geometryで必要になるGPU storage容量変更をRHI共通操作として扱います。
    // OpenGLではVAOがBuffer object名を保持するため同一handleのstorageを再確保し、
    // DirectX/Vulkan Backendでは各APIに適した方法でResourceを更新できる境界にします。
    virtual void Resize(std::size_t size, const void* data = nullptr) = 0;

    virtual const RHIBufferSpecification& GetSpecification() const = 0;
};

} // namespace Raven
