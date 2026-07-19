#pragma once

#include "Raven/Renderer/Buffer/VertexBuffer.h"

namespace Raven
{

class OpenGLVertexBuffer : public VertexBuffer
{
public:
    OpenGLVertexBuffer(float* vertices, uint32_t size);
    virtual ~OpenGLVertexBuffer();

    virtual void Bind() const override;
    virtual void Unbind() const override;

    virtual const BufferLayout& GetLayout() const override;
    virtual void SetLayout(const BufferLayout& layout) override;

private:
    uint32_t m_RendererID;
    BufferLayout m_Layout;
};

}