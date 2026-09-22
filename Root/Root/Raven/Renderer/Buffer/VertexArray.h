#pragma once

#include <vector>

#include "Raven/Renderer/Buffer/VertexBuffer.h"
#include "Raven/Renderer/Buffer/IndexBuffer.h"

namespace Raven
{
class VertexArray
{

public:
    virtual ~VertexArray() = default;

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;

    virtual void AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer) = 0;

    virtual void SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer) = 0;

    virtual const Ref<IndexBuffer>& GetIndexBuffer() const = 0;

    // VAOはOpenGL Context間で共有できないため、共有VBO/EBOから現在のContext用VAOを再構築します。
    // 非対応Backendではnullptr。返却したVAOは生成先Contextが生存する間に破棄してください。
    virtual Ref<VertexArray> CloneForCurrentContext() const { return nullptr; }

    static Ref<VertexArray> Create();
};

}