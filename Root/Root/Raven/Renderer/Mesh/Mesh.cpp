#include "Raven/Renderer/Mesh/Mesh.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"
#include "Raven/Renderer/RenderCommand.h"

namespace Raven
{

Mesh::Mesh(Ref<MeshGeometry> geometry)
    : m_Geometry(std::move(geometry))
{
    BuildRenderResources();
}

Mesh::Mesh(Ref<VertexArray> vertexArray, int32_t indexCount)
    : m_VertexArray(std::move(vertexArray)),
      m_IndexCount(indexCount > 0 ? static_cast<uint32_t>(indexCount) : 0u)
{
}

void Mesh::BuildRenderResources()
{
    m_VertexArray = nullptr;
    m_IndexCount = 0;

    if (!m_Geometry || m_Geometry->GetVertices().empty())
    {
        return;
    }

    const auto& vertices = m_Geometry->GetVertices();
    const auto& indices = m_Geometry->GetIndices();

    // ========================================================================
    // MeshVertex -> GPU upload data
    // ========================================================================
    // 現在のVertexBuffer::Createはfloat配列を受け取るAPIなので、MeshVertexを直接
    // reinterpret_castせず明示的にfloat列へ展開します。
    // こうしておけばMath型のpadding/alignmentが将来変わってもGPU strideが壊れません。
    std::vector<float> vertexData;
    vertexData.reserve(vertices.size() * 8);

    for (const MeshVertex& vertex : vertices)
    {
        vertexData.insert(vertexData.end(), {
            vertex.Position.x, vertex.Position.y, vertex.Position.z,
            vertex.Color.x, vertex.Color.y, vertex.Color.z,
            vertex.TexCoord.x, vertex.TexCoord.y
        });
    }

    m_VertexArray = VertexArray::Create();

    auto vertexBuffer = VertexBuffer::Create(
        vertexData.data(),
        static_cast<uint32_t>(vertexData.size() * sizeof(float)));

    vertexBuffer->SetLayout({
        { ShaderDataType::Float3, "a_Position" },
        { ShaderDataType::Float3, "a_Color" },
        { ShaderDataType::Float2, "a_Texcord" }
    });

    m_VertexArray->AddVertexBuffer(vertexBuffer);

    if (!indices.empty())
    {
        auto indexBuffer = IndexBuffer::Create(
            indices.data(),
            static_cast<uint32_t>(indices.size()));

        m_VertexArray->SetIndexBuffer(indexBuffer);
        m_IndexCount = static_cast<uint32_t>(indices.size());
    }
}

void Mesh::Draw() const
{
    if (!m_VertexArray)
    {
        return;
    }

    RenderCommand::DrawIndexed(m_VertexArray, m_IndexCount);
}

} // namespace Raven
