#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Buffer/VertexArray.h"

namespace Raven
{

class Mesh
{

public:
    void Draw() const;

private:
    Ref<VertexArray> m_VertexArray;
    Ref<VertexBuffer> m_VertexBuffer;
    Ref<IndexBuffer> m_IndexBuffer;
    uint32_t m_IndexCount = 0;
};

}