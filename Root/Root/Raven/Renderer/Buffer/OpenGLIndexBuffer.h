#pragma once

#include "Raven/Renderer/Buffer/IndexBuffer.h"

namespace Raven
{

class OpenGLIndexBuffer : public IndexBuffer
{
public:

    OpenGLIndexBuffer(const uint32_t* indices, uint32_t count);

    virtual ~OpenGLIndexBuffer();

    virtual void Bind() const override;
    virtual void Unbind() const override;

    virtual uint32_t GetCount() const override
    {
        return m_Count;
    }

private:

    uint32_t m_RendererID;
    uint32_t m_Count;
};

}