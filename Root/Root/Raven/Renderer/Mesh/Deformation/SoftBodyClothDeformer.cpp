#include "Raven/Renderer/Mesh/Deformation/SoftBodyClothDeformer.h"

#include <algorithm>
#include <vector>

#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven
{

SoftBodyClothDeformer::SoftBodyClothDeformer(uint32_t rows, uint32_t columns)
    : m_Rows(rows),
      m_Columns(columns)
{
    ph::SoftBodySolverSettings solverSettings{};
    solverSettings.SolverIterations = 12u;
    solverSettings.CollisionThickness = 0.005f;
    m_Solver.SetSettings(solverSettings);

    // 最初の目視確認用に、Cloth中央より少し下へ静的Sphereを置きます。
    // Scene側からSetCollisionSphere()を呼べば任意の位置・半径へ差し替えられます。
    SetCollisionSphere({ 0.0f, -0.12f, 0.0f }, 0.20f);
}

void SoftBodyClothDeformer::SetCollisionSphere(const math::Vec3& center, float radius)
{
    m_CollisionSphereEnabled = radius > 0.0f;
    m_CollisionSphereCenter = center;
    m_CollisionSphereRadius = std::max(0.0f, radius);

    if (m_Initialized)
    {
        ApplyCollisionSphereToSolver();
    }
}

void SoftBodyClothDeformer::DisableCollisionSphere()
{
    m_CollisionSphereEnabled = false;
    m_CollisionSphereRadius = 0.0f;

    if (m_Initialized)
    {
        m_Solver.ClearSphereColliders();
    }
}

void SoftBodyClothDeformer::ApplyCollisionSphereToSolver()
{
    m_Solver.ClearSphereColliders();

    if (m_CollisionSphereEnabled == false || m_CollisionSphereRadius <= 0.0f)
    {
        return;
    }

    m_CollisionSphereIndex = m_Solver.AddSphereCollider(
        m_CollisionSphereCenter,
        m_CollisionSphereRadius);
}

bool SoftBodyClothDeformer::InitializeFromMesh(Mesh& mesh)
{
    const Ref<MeshGeometry>& geometry = mesh.GetGeometry();
    if (geometry == nullptr || geometry->GetGeometryUsage() != GeometryUsage::Dynamic)
    {
        return false;
    }

    const std::vector<MeshVertex>& vertices = geometry->GetVertices();
    const size_t expectedVertexCount =
        static_cast<size_t>(m_Rows + 1u) * static_cast<size_t>(m_Columns + 1u);

    if (vertices.size() != expectedVertexCount || m_Rows == 0u || m_Columns == 0u)
    {
        return false;
    }

    // DynamicGridはXZ平面ですが、ClothはXY平面に立てて初期化します。
    // Gridのrow-major順序はCloth Builderと一致するため、頂点IndexとParticleIndexを
    // そのまま対応付けられます。
    ph::SoftBodyClothSettings clothSettings{};
    clothSettings.Rows = m_Rows;
    clothSettings.Columns = m_Columns;
    clothSettings.Width = 1.0f;
    clothSettings.Height = 1.0f;
    clothSettings.InverseMass = 1.0f;
    clothSettings.StructuralCompliance = 0.000001f;
    clothSettings.ShearCompliance = 0.000002f;
    clothSettings.PinTopLeft = true;
    clothSettings.PinTopRight = true;

    m_Solver.Clear();
    m_Cloth = ph::SoftBodyClothBuilder::Build(m_Solver, clothSettings);
    m_Initialized = true;

    // Solver::Clear()でColliderも消えるため、Cloth構築後に現在の設定を再登録します。
    ApplyCollisionSphereToSolver();
    return true;
}

void SoftBodyClothDeformer::Update(Mesh& mesh, float deltaTime)
{
    if (m_Initialized == false)
    {
        if (InitializeFromMesh(mesh) == false)
        {
            return;
        }
    }

    const Ref<MeshGeometry>& geometry = mesh.GetGeometry();
    if (geometry == nullptr || geometry->GetGeometryUsage() != GeometryUsage::Dynamic)
    {
        return;
    }

    m_Solver.Step(deltaTime);

    const std::vector<ph::SoftBodyParticle>& particles = m_Solver.GetParticles();
    const std::vector<MeshVertex>& sourceVertices = geometry->GetVertices();
    if (sourceVertices.size() != m_Cloth.ParticleIndices.size())
    {
        return;
    }

    std::vector<MeshVertex> deformedVertices = sourceVertices;
    for (size_t vertexIndex = 0u; vertexIndex < deformedVertices.size(); ++vertexIndex)
    {
        const uint32_t particleIndex = m_Cloth.ParticleIndices[vertexIndex];
        if (particleIndex >= particles.size())
        {
            return;
        }

        deformedVertices[vertexIndex].Position = particles[particleIndex].Position;
    }

    if (geometry->SetVertices(std::move(deformedVertices)) == false)
    {
        return;
    }

    mesh.SyncGeometry();
}

} // namespace Raven
