#pragma once

#include <cstdint>

namespace Raven
{

// Scene描画で使う頂点/Index Bufferの共通参照契約です。
// GPUメモリの所有・更新・native handleは各Backendに残し、
// CommandListはこの型からBackend固有のBufferを検証して利用します。
class RHISceneBuffer
{
public:
    virtual ~RHISceneBuffer() = default;

    virtual bool IsValid() const = 0;
    virtual bool IsIndexBuffer() const = 0;
    virtual uint32_t GetIndexCount() const = 0;
    virtual uint32_t GetVertexStride() const = 0;
};

} // namespace Raven
