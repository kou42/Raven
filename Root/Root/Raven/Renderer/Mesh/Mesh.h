#pragma once

#include "Raven/Core/Base.h"

namespace Raven
{

class VertexArray;

class Mesh
{

public:

    Mesh(Ref<VertexArray> vertexArray, int32_t indexCount = 0)
    {
        m_VertexArray = std::move(vertexArray);
        m_IndexCount = indexCount;
    }

    void Draw() const;

    const Ref<VertexArray>& GetVertexArray() const
    {
        return m_VertexArray;
    }

    uint32_t GetIndexCount() const
    {
        return m_IndexCount;
    }

private:
    Ref<VertexArray> m_VertexArray;
    uint32_t m_IndexCount = 0;
};

}