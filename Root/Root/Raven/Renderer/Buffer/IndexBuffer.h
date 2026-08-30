#pragma once

#include "Raven/Core/Base.h"

namespace Raven
{

class IndexBuffer
{
public:
    virtual ~IndexBuffer() = default;

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;

    // ========================================================================
    // Dynamic index update
    // ========================================================================
    // UI / Debug GeometryのようにframeごとにIndex列が変化する描画でも、
    // IndexBuffer抽象を維持したままGPU Bufferを再利用できるようにします。
    // countは「byte数」ではなくuint32_t indexの要素数です。
    virtual void SetData(const uint32_t* indices, uint32_t count) = 0;

    virtual uint32_t GetCount() const = 0;

    static Ref<IndexBuffer> Create(const uint32_t* indices, uint32_t count);
};

}
