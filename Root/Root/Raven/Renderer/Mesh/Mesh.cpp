#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Renderer.h"

namespace Raven
{

void Mesh::Draw() const
{
#if 0
    m_VertexArray->Bind();

    glDrawElements(
        GL_TRIANGLES,
        m_IndexCount,
        GL_UNSIGNED_INT,
        nullptr
    );
#else
    Renderer::DrawIndexed(m_VertexArray);
#endif

}

}