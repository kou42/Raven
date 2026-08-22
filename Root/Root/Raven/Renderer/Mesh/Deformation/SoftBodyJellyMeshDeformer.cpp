#include "Raven/Renderer/Mesh/Deformation/SoftBodyJellyMeshDeformer.h"

#include <cassert>
#include <cstddef>
#include <unordered_map>
#include <utility>

#include "Raven/Renderer/Mesh/Mesh.h"

namespace Raven
{

SoftBodyJellyMeshDeformer::SoftBodyJellyMeshDeformer(
    const ph::SoftBodyJellySettings& settings,
    const math::Vec3& color)
    : m_Color(color)
{
    ph::SoftBodySolverSettings solverSettings{};

    // JellyはDistance / Volume / Collisionが強く連成するため、Clothと同程度の反復回数を
    // 初期値にします。Material調整で硬めのVolumeを使用しても収束しやすい値です。
    solverSettings.SolverIterations = 12u;
    solverSettings.CollisionThickness = 0.005f;
    m_Solver.SetSettings(solverSettings);

    m_Jelly = ph::SoftBodyJellyBuilder::Build(m_Solver, settings);
    m_Surface = ph::SoftBodyJellySurfaceBuilder::Build(m_Jelly);
}

void SoftBodyJellyMeshDeformer::SetCollisionPlane(const math::Vec3& normal, float offset)
{
    // 現段階ではJelly Deformerが1枚の床Planeを管理する最小構成です。
    // Normalの正規化やゼロベクトルfallbackはSoftBodySolver側へ集約します。
    m_Solver.ClearPlaneColliders();
    m_Solver.AddPlaneCollider(normal, offset);
}

void SoftBodyJellyMeshDeformer::DisableCollisionPlane()
{
    m_Solver.ClearPlaneColliders();
}

void SoftBodyJellyMeshDeformer::Update(Mesh& mesh, float deltaTime)
{
    if (deltaTime <= 0.0f)
    {
        return;
    }

    const Ref<MeshGeometry>& geometry = mesh.GetGeometry();
    if (geometry == nullptr)
    {
        return;
    }

    if (geometry->GetGeometryUsage() != GeometryUsage::Dynamic)
    {
        return;
    }

    // ========================================================================
    // Physics Step
    // ========================================================================
    // Cloth Deformerと同様にMeshDeformationSystemのUpdate入口からSimulationまで進めます。
    // StepSoftBodyJelly()内部では Distance -> Volume -> Collision を同一XPBD iterationで解き、
    // 最後にJelly MaterialのVelocity Dampingを適用します。
    ph::StepSoftBodyJelly(m_Solver, m_Jelly, deltaTime);

    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    if (BuildVerticesAndIndices(vertices, indices) == false)
    {
        return;
    }

    // Surface TopologyはFixedなので、Update時は既存Index Bufferをそのまま使えます。
    // BuildVerticesAndIndices()で生成したindicesはNormal再計算だけに利用し、GPU Indexの再Uploadは行いません。
    if (vertices.size() != geometry->GetVertices().size())
    {
        return;
    }

    RecalculateNormals(vertices, indices);

    if (geometry->SetVertices(std::move(vertices)) == false)
    {
        return;
    }

    mesh.SyncGeometry();
}

Ref<MeshGeometry> SoftBodyJellyMeshDeformer::CreateGeometry() const
{
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    if (BuildVerticesAndIndices(vertices, indices) == false)
    {
        return nullptr;
    }

    RecalculateNormals(vertices, indices);

    return CreateRef<MeshGeometry>(
        std::move(vertices),
        std::move(indices),
        GeometryUsage::Dynamic,
        TopologyUsage::Fixed);
}

bool SoftBodyJellyMeshDeformer::BuildVerticesAndIndices(
    std::vector<MeshVertex>& outVertices,
    std::vector<uint32_t>& outIndices) const
{
    outVertices.clear();
    outIndices.clear();

    const std::vector<ph::SoftBodyParticle>& particles = m_Solver.GetParticles();
    if (m_Surface.SurfaceParticleIndices.empty() || m_Surface.Triangles.empty())
    {
        return false;
    }

    // ========================================================================
    // Solver Particle Index -> Mesh Vertex Index
    // ========================================================================
    // SurfaceParticleIndicesはPhysics側Builderでsort済みなので、同一Topologyから毎回同じ
    // Mesh Vertex Index対応が得られます。
    std::unordered_map<uint32_t, uint32_t> particleToVertex;
    particleToVertex.reserve(m_Surface.SurfaceParticleIndices.size());

    outVertices.reserve(m_Surface.SurfaceParticleIndices.size());

    for (uint32_t particleIndex : m_Surface.SurfaceParticleIndices)
    {
        if (particleIndex >= particles.size())
        {
            outVertices.clear();
            outIndices.clear();
            return false;
        }

        const uint32_t vertexIndex = static_cast<uint32_t>(outVertices.size());
        particleToVertex.emplace(particleIndex, vertexIndex);

        MeshVertex vertex{};
        vertex.Position = particles[particleIndex].Position;
        vertex.Color = m_Color;
        vertex.TexCoord = math::Vec2{};
        vertex.Normal = math::Vec3{};
        outVertices.push_back(vertex);
    }

    outIndices.reserve(m_Surface.Triangles.size() * 3u);

    for (const ph::SoftBodyJellySurfaceTriangle& triangle : m_Surface.Triangles)
    {
        const auto iteratorA = particleToVertex.find(triangle.ParticleA);
        const auto iteratorB = particleToVertex.find(triangle.ParticleB);
        const auto iteratorC = particleToVertex.find(triangle.ParticleC);

        if (iteratorA == particleToVertex.end()
            || iteratorB == particleToVertex.end()
            || iteratorC == particleToVertex.end())
        {
            outVertices.clear();
            outIndices.clear();
            return false;
        }

        outIndices.push_back(iteratorA->second);
        outIndices.push_back(iteratorB->second);
        outIndices.push_back(iteratorC->second);
    }

    return true;
}

void SoftBodyJellyMeshDeformer::RecalculateNormals(
    std::vector<MeshVertex>& vertices,
    const std::vector<uint32_t>& indices) const
{
    for (MeshVertex& vertex : vertices)
    {
        vertex.Normal = math::Vec3{};
    }

    // ========================================================================
    // Area-weighted Vertex Normal
    // ========================================================================
    // cross(edgeAB, edgeAC) は長さがTriangle面積の2倍なので、正規化前のFace Normalを
    // そのまま各頂点へ加算すると大きい面ほど寄与が大きい面積加重平均になります。
    for (std::size_t index = 0u; index + 2u < indices.size(); index += 3u)
    {
        const uint32_t indexA = indices[index + 0u];
        const uint32_t indexB = indices[index + 1u];
        const uint32_t indexC = indices[index + 2u];

        assert(indexA < vertices.size());
        assert(indexB < vertices.size());
        assert(indexC < vertices.size());

        const math::Vec3 edgeAB = vertices[indexB].Position - vertices[indexA].Position;
        const math::Vec3 edgeAC = vertices[indexC].Position - vertices[indexA].Position;
        const math::Vec3 faceNormal = math::Vec3::Cross(edgeAB, edgeAC);

        if (faceNormal.LengthSq() <= math::Epsilon * math::Epsilon)
        {
            continue;
        }

        vertices[indexA].Normal += faceNormal;
        vertices[indexB].Normal += faceNormal;
        vertices[indexC].Normal += faceNormal;
    }

    for (MeshVertex& vertex : vertices)
    {
        if (vertex.Normal.LengthSq() <= math::Epsilon * math::Epsilon)
        {
            // 退化面しか接続されていない異常TopologyでもNaNを出さないfallbackです。
            vertex.Normal = math::Vec3{ 0.0f, 1.0f, 0.0f };
            continue;
        }

        vertex.Normal.Normalize();
    }
}

} // namespace Raven
