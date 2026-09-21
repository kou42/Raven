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

    // GPU同期や範囲検証を含む更新結果を、Backend固有型へcastせず共通層へ返します。
    // 既存のvoid APIは互換性のため残し、結果を扱わない呼び出しをTry版へ転送します。
    virtual bool TrySetData(
        const void* data, std::size_t size, std::size_t offset = 0) = 0;

    virtual void SetData(
        const void* data, std::size_t size, std::size_t offset = 0)
    {
        (void)TrySetData(data, size, offset);
    }

    // Dynamic Geometryで必要になるGPU storage容量変更をRHI共通操作として扱います。
    // OpenGLではVAOがBuffer object名を保持するため同一handleのstorageを再確保し、
    // DirectX/Vulkan Backendでは各APIに適した方法でResourceを更新できる境界にします。
    virtual bool TryResize(std::size_t size, const void* data = nullptr) = 0;

    virtual void Resize(std::size_t size, const void* data = nullptr)
    {
        (void)TryResize(size, data);
    }

    virtual const RHIBufferSpecification& GetSpecification() const = 0;
};

} // namespace Raven
