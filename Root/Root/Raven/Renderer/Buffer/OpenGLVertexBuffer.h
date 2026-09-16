#pragma once

#include "Raven/Renderer/Buffer/VertexBuffer.h"

namespace Raven
{

class OpenGLRHIBuffer;

// 既存VertexBuffer APIと新しいRHI Bufferを接続する互換Bridgeです。
// VAO構築側がまだBind/Unbindを必要とするためOpenGL binding操作はここへ残し、
// GPU Bufferの生成・データ更新・破棄はOpenGLRHIBufferへ委譲します。
class OpenGLVertexBuffer : public VertexBuffer
{
public:
    OpenGLVertexBuffer(const float* vertices, uint32_t size);
    ~OpenGLVertexBuffer() override = default;

    void Bind() const override;
    void Unbind() const override;

    void SetData(const void* data, uint32_t size) override;

    const BufferLayout& GetLayout() const override;
    void SetLayout(const BufferLayout& layout) override;

private:
    void RecreateBuffer(const void* data, uint32_t size);

private:
    Ref<OpenGLRHIBuffer> m_RHIBuffer;
    BufferLayout m_Layout;

    // 現在GPU側に確保しているVBO容量(byte)。
    // 容量を超えた場合だけRHIBufferを再生成し、通常更新では同じResourceを再利用します。
    uint32_t m_Capacity = 0;
};

} // namespace Raven
