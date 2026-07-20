#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Buffer/VertexArray.h"

namespace Raven
{

void Mesh::Draw() const
{
    
    if (!m_VertexArray) {
        return;
    }

    RenderCommand::DrawIndexed(m_VertexArray, m_IndexCount);
   
}

}