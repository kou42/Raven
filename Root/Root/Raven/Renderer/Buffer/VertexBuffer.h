#pragma once

#include "../../Core/Base.h"

namespace Raven
{

class VertexBuffer
{
public:
    virtual ~VertexBuffer() = default;

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;

    static Ref<VertexBuffer> Create(float* vertices, uint32_t size);

};

}