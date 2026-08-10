#pragma once

#include "Raven/Renderer/Buffer/VertexBuffer.h"

namespace Raven
{

class OpenGLVertexBuffer : public VertexBuffer
{
public:
    OpenGLVertexBuffer(const float* vertices, uint32_t size);
    virtual ~OpenGLVertexBuffer();

    virtual void Bind() const override;
    virtual void Unbind() const override;

    virtual void SetData(const void* data, uint32_t size) override;

    virtual const BufferLayout& GetLayout() const override;
    virtual void SetLayout(const BufferLayout& layout) override;

private:
    uint32_t m_RendererID;
    BufferLayout m_Layout;

    // 現在GPU側に確保しているVBO容量(byte)。
    // Dynamic Geometry更新時、容量内ならglBufferSubDataで内容だけを書き換えます。
    uint32_t m_Capacity = 0;
};

}