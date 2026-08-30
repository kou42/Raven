#include "Raven/Renderer/Mesh/Mesh.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"
#include "Raven/Renderer/RenderCommand.h"

namespace Raven
{
namespace
{
void BuildVertexUploadData(
    const std::vector<MeshVertex>& vertices,
    std::vector<float>& outVertexData)
{
    // ========================================================================
    // MeshVertex -> GPU upload data
    // ========================================================================
    // 現在のVertexBuffer APIはfloat配列を受け取るため、MeshVertexを直接reinterpret_castせず
    // 明示的にfloat列へ展開します。Math型にpadding/alignmentが追加されてもGPU strideを
    // 安定させられ、初期UploadとDynamic更新で同じレイアウト規約を共有できます。
    //
    // glTFのHuman Meshで利用するNormalもここで明示的に転送します。
    // 1頂点 = Position(3) + Color(3) + TexCoord(2) + Normal(3) = 11 floatです。
    outVertexData.resize(vertices.size() * 11u);

    for (std::size_t vertexIndex = 0u; vertexIndex < vertices.size(); ++vertexIndex)
    {
        const MeshVertex& vertex = vertices[vertexIndex];
        const std::size_t outputIndex = vertexIndex * 11u;
        outVertexData[outputIndex] = vertex.Position.x;
        outVertexData[outputIndex + 1u] = vertex.Position.y;
        outVertexData[outputIndex + 2u] = vertex.Position.z;
        outVertexData[outputIndex + 3u] = vertex.Color.x;
        outVertexData[outputIndex + 4u] = vertex.Color.y;
        outVertexData[outputIndex + 5u] = vertex.Color.z;
        outVertexData[outputIndex + 6u] = vertex.TexCoord.x;
        outVertexData[outputIndex + 7u] = vertex.TexCoord.y;
        outVertexData[outputIndex + 8u] = vertex.Normal.x;
        outVertexData[outputIndex + 9u] = vertex.Normal.y;
        outVertexData[outputIndex + 10u] = vertex.Normal.z;
    }
}
} // namespace

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
    m_VertexBuffer = nullptr;
    m_IndexCount = 0;
    m_UploadedGeometryRevision = 0;

    if (m_Geometry == nullptr || m_Geometry->GetVertices().empty())
    {
        return;
    }

    const auto& indices = m_Geometry->GetIndices();
    BuildVertexUploadData(m_Geometry->GetVertices(), m_VertexUploadData);

    m_VertexArray = VertexArray::Create();

    m_VertexBuffer = VertexBuffer::Create(
        m_VertexUploadData.data(),
        static_cast<uint32_t>(m_VertexUploadData.size() * sizeof(float)));

    // Attributeの順序はBuildVertexUploadData()と必ず一致させます。
    // a_Normalはlocationを自動採番するOpenGLVertexArray側で4番目のattributeになります。
    m_VertexBuffer->SetLayout({
        { ShaderDataType::Float3, "a_Position" },
        { ShaderDataType::Float3, "a_Color" },
        { ShaderDataType::Float2, "a_Texcord" },
        { ShaderDataType::Float3, "a_Normal" }
    });

    m_VertexArray->AddVertexBuffer(m_VertexBuffer);

    if (indices.empty() == false)
    {
        auto indexBuffer = IndexBuffer::Create(
            indices.data(),
            static_cast<uint32_t>(indices.size()));

        m_VertexArray->SetIndexBuffer(indexBuffer);
        m_IndexCount = static_cast<uint32_t>(indices.size());
    }

    m_UploadedGeometryRevision = m_Geometry->GetRevision();
}

bool Mesh::UploadVertexData()
{
    if (m_Geometry == nullptr || m_VertexBuffer == nullptr || m_Geometry->GetVertices().empty())
    {
        return false;
    }

    BuildVertexUploadData(m_Geometry->GetVertices(), m_VertexUploadData);

    m_VertexBuffer->SetData(
        m_VertexUploadData.data(),
        static_cast<uint32_t>(m_VertexUploadData.size() * sizeof(float)));

    m_UploadedGeometryRevision = m_Geometry->GetRevision();
    return true;
}

bool Mesh::SyncGeometry()
{
    if (m_Geometry == nullptr
        || m_Geometry->GetGeometryUsage() != GeometryUsage::Dynamic
        || m_VertexBuffer == nullptr)
    {
        return false;
    }

    // CPU Geometryに変更が無い場合はVBO Uploadを行いません。
    // Deformer側は毎フレームSyncGeometry()を呼べるため、呼び出し側でdirty管理を重複して
    // 実装する必要がありません。
    if (m_UploadedGeometryRevision == m_Geometry->GetRevision())
    {
        return false;
    }

    // この段階ではFixed TopologyのDynamic Geometryだけを対象にしています。
    // Dynamic TopologyはIndexBuffer更新とVAO再構築の責務が増えるため、頂点変形とは分離して
    // 後続実装で追加します。
    if (m_Geometry->GetTopologyUsage() != TopologyUsage::Fixed)
    {
        return false;
    }

    return UploadVertexData();
}

void Mesh::Draw() const
{
    if (m_VertexArray == nullptr)
    {
        return;
    }

    RenderCommand::DrawIndexed(m_VertexArray, m_IndexCount);
}

} // namespace Raven
