#pragma once

#include "Raven/Renderer/Buffer/IndexBuffer.h"
#include "Raven/Renderer/RHI/RHIBuffer.h"

namespace Raven
{

// 既存IndexBuffer APIとRHIBufferを接続する互換Bridgeです。
// EBOのVAO関連付けだけはVertexArray側の責務として維持し、Resource管理をRHIDevice / RHIへ移します。
class OpenGLIndexBuffer : public IndexBuffer
{
public:
    OpenGLIndexBuffer(const uint32_t* indices, uint32_t count);
    ~OpenGLIndexBuffer() override = default;

    void Bind() const override;
    void Unbind() const override;
    void SetData(const uint32_t* indices, uint32_t count) override;

    uint32_t GetCount() const override
    {
        return m_Count;
    }

private:
    void RecreateBuffer(const uint32_t* indices, uint32_t count);

private:
    Ref<RHIBuffer> m_RHIBuffer;
    uint32_t m_Count = 0;

    // GPU側へ確保済みのindex容量です。
    // 容量を超えた場合もEBO object名を維持し、storageだけを拡張します。
    uint32_t m_Capacity = 0;
};

} // namespace Raven
