#pragma once

#include "VertexArray.h"
//#include "../../Core/Base.h"

namespace Raven
{

class OpenGLVertexArray : public VertexArray
{
public:
    OpenGLVertexArray();
    virtual ~OpenGLVertexArray();

    virtual void Bind() const override;
    virtual void Unbind() const override;

    virtual void AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer) override;

    virtual void SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer) override;

    virtual const Ref<IndexBuffer>& GetIndexBuffer() const override
    {
        return m_IndexBuffer;
    }

private:
    uint32_t m_RendererID;

    std::vector<Ref<VertexBuffer>> m_VertexBuffers;
    Ref<IndexBuffer> m_IndexBuffer;
};

}