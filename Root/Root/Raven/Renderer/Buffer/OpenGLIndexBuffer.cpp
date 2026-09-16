#include "Raven/Renderer/Buffer/OpenGLIndexBuffer.h"

#include <glad/glad.h>

#include "Raven/Platform/OpenGL/RHI/OpenGLRHIBuffer.h"
#include "Raven/Renderer/RHI/RHIDevice.h"
#include "Raven/Renderer/RenderCommand.h"

namespace Raven
{

OpenGLIndexBuffer::OpenGLIndexBuffer(const uint32_t* indices, uint32_t count)
{
    RecreateBuffer(indices, count);
}

void OpenGLIndexBuffer::Bind() const
{
    if (m_RHIBuffer == nullptr)
    {
        return;
    }

    const auto openGLBuffer = std::dynamic_pointer_cast<OpenGLRHIBuffer>(m_RHIBuffer);
    if (openGLBuffer == nullptr)
    {
        return;
    }

    // GL_ELEMENT_ARRAY_BUFFER は VAO 状態に紐づくため、
    // Resource生成・更新では触らず、VertexArray::SetIndexBufferから呼ばれる
    // この互換経路だけでEBOを対象VAOへ関連付けます。
    // RHI Resource生成側ではGL_COPY_WRITE_BUFFERを使用するため、以前必要だった
    // 「一時的にVAO 0をbindしてから初期化する」回避処理もBackend側へ安全に置き換えられています。
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, openGLBuffer->GetRendererID());
}

void OpenGLIndexBuffer::Unbind() const
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void OpenGLIndexBuffer::SetData(const uint32_t* indices, uint32_t count)
{
    if (count == 0)
    {
        m_Count = 0;
        return;
    }

    if (m_RHIBuffer == nullptr)
    {
        RecreateBuffer(indices, count);
        return;
    }

    const std::size_t dataSize = static_cast<std::size_t>(count) * sizeof(uint32_t);

    // ========================================================================
    // Dynamic update path
    // ========================================================================
    // 既存容量へ収まる場合はBuffer Object自体を再生成せず、内容だけを更新します。
    // UI DrawListは毎frame再構築されるため、このFast Pathが通常経路になります。
    // RHI移行後はRHIBuffer::SetDataがBackend内部でglBufferSubData相当の更新を担当します。
    if (indices != nullptr && count <= m_Capacity)
    {
        m_RHIBuffer->SetData(indices, dataSize);
        m_Count = count;
        return;
    }

    // 容量不足時のみGPU領域を拡張します。
    // 以降の更新は新しい容量内でSetDataのFast Pathへ戻ります。
    //
    // EBO object名はVAOに保存されるため、容量拡張時もResource object自体は交換しません。
    // BackendのRHIBuffer::Resize()へstorage再確保を委譲し、既存VAOとの関連付けを維持します。
    m_RHIBuffer->Resize(dataSize, indices);
    m_Count = count;
    m_Capacity = count;
}

void OpenGLIndexBuffer::RecreateBuffer(const uint32_t* indices, uint32_t count)
{
    if (count == 0)
    {
        m_RHIBuffer = nullptr;
        m_Count = 0;
        m_Capacity = 0;
        return;
    }

    RHIDevice* device = RenderCommand::GetDevice();
    if (device == nullptr)
    {
        m_RHIBuffer = nullptr;
        m_Count = 0;
        m_Capacity = 0;
        return;
    }

    RHIBufferSpecification specification{};
    specification.Size = static_cast<std::size_t>(count) * sizeof(uint32_t);
    specification.Usage = RHIBufferUsage::Index;
    specification.MemoryUsage = RHIMemoryUsage::Dynamic;
    specification.DebugName = "Legacy IndexBuffer Bridge";

    // VertexBufferと同様、Resource生成は現在選択されているRHIDeviceへ集約します。
    m_RHIBuffer = device->CreateBuffer(specification, indices);
    if (m_RHIBuffer == nullptr)
    {
        m_Count = 0;
        m_Capacity = 0;
        return;
    }

    m_Count = count;
    m_Capacity = count;
}

} // namespace Raven
