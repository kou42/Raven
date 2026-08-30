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
    virtual void SetData(const uint32_t* indices, uint32_t count) override;

    virtual uint32_t GetCount() const override
    {
        return m_Count;
    }

private:
    uint32_t m_RendererID = 0;
    uint32_t m_Count = 0;

    // GPU側へ確保済みのindex容量です。
    // UIのように毎frame要素数が変わる場合でも、容量内ならglBufferSubDataで再利用します。
    uint32_t m_Capacity = 0;
};

}
