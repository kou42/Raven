#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Buffer/BufferLayout.h"

namespace Raven
{

class VertexBuffer
{
public:
    virtual ~VertexBuffer() = default;

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;

    virtual const BufferLayout& GetLayout() const = 0;
    virtual void SetLayout(const BufferLayout& layout) = 0;

    static Ref<VertexBuffer> Create(float* vertices, uint32_t size);

};

}