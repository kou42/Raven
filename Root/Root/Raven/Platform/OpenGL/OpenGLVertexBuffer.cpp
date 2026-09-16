#include "Raven/Platform/OpenGL/OpenGLVertexBuffer.h"

#include <glad/glad.h>

#include "Raven/Platform/OpenGL/RHI/OpenGLRHIBuffer.h"
#include "Raven/Renderer/RHI/RHIDevice.h"
#include "Raven/Renderer/RenderCommand.h"

namespace Raven
{

OpenGLVertexBuffer::OpenGLVertexBuffer(const float* vertices, uint32_t size)
{
    RecreateBuffer(vertices, size);
}

void OpenGLVertexBuffer::Bind() const
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

    // VAOの頂点属性定義は現在もGL_ARRAY_BUFFER bindingを参照するため、
    // CommandList移行までの互換処理としてnative handleをBackend内部でbindします。
    glBindBuffer(GL_ARRAY_BUFFER, openGLBuffer->GetRendererID());
}

void OpenGLVertexBuffer::Unbind() const
{
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void OpenGLVertexBuffer::SetData(const void* data, uint32_t size)
{
    if (size == 0)
    {
        return;
    }

    if (m_RHIBuffer == nullptr)
    {
        RecreateBuffer(data, size);
        return;
    }

    // ========================================================================
    // Dynamic update path
    // ========================================================================
    // 既存容量に収まる通常の頂点変形(Skeletal / SoftBody / Morph)では、VBOを再確保せず
    // 内容だけを更新します。Fixed TopologyのDynamic Geometryでは頂点数が変わらないため、
    // 基本的にこの経路を通ります。
    // RHI移行後はRHIBuffer::SetDataがBackend内部でglBufferSubData相当の更新を担当します。
    if (data != nullptr && size <= m_Capacity)
    {
        m_RHIBuffer->SetData(data, size);
        return;
    }

    // 容量を超える場合、または空データで明示的に領域だけ確保したい場合は再確保します。
    // 再確保後は更新用途であることをDriverへ伝えるためDynamic用途を維持します。
    //
    // glVertexAttribPointerは設定時のBuffer object名をVAOへ保持します。
    // 容量拡張でRHIBuffer objectを交換すると既存VAOが古いBufferを参照するため、
    // OpenGL BackendではRHIBuffer::Resize()により同一objectのstorageだけを拡張します。
    m_RHIBuffer->Resize(size, data);
    m_Capacity = size;
}

void OpenGLVertexBuffer::SetLayout(const BufferLayout& layout)
{
    m_Layout = layout;
}

const BufferLayout& OpenGLVertexBuffer::GetLayout() const
{
    return m_Layout;
}

void OpenGLVertexBuffer::RecreateBuffer(const void* data, uint32_t size)
{
    if (size == 0)
    {
        m_RHIBuffer = nullptr;
        m_Capacity = 0;
        return;
    }

    RHIDevice* device = RenderCommand::GetDevice();
    if (device == nullptr)
    {
        m_RHIBuffer = nullptr;
        m_Capacity = 0;
        return;
    }

    RHIBufferSpecification specification{};
    specification.Size = size;
    specification.Usage = RHIBufferUsage::Vertex;
    specification.MemoryUsage = RHIMemoryUsage::Dynamic;
    specification.DebugName = "Legacy VertexBuffer Bridge";

    // GPU Resource生成はBackend型を直接newせず、現在のRHIDeviceへ委譲します。
    m_RHIBuffer = device->CreateBuffer(specification, data);
    if (m_RHIBuffer == nullptr)
    {
        m_Capacity = 0;
        return;
    }

    m_Capacity = size;
}

} // namespace Raven
