#include "Raven/Renderer/Mesh/Deformation/SoftBodyClothDeformer.h"

#include <algorithm>
#include <vector>

#include "Raven/Physics/SoftBody/SoftBodyParticleTriangleSelfCollision.h"
#include "Raven/Physics/SoftBody/SoftBodySelfCollision.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven
{
namespace
{
// ============================================================================
// RecalculateClothNormals
// ============================================================================
// SoftBodyでPositionが毎フレーム変化すると、Primitive生成時のNormalは現在形状を表さなくなります。
// Fixed TopologyのIndexを使って各Triangleの面法線を頂点へ蓄積し、最後に正規化して
// Smooth Normalを再構築します。
void RecalculateClothNormals(
    std::vector<MeshVertex>& vertices,
    const std::vector<uint32_t>& indices)
{
    for (MeshVertex& vertex : vertices)
    {
        vertex.Normal = math::Vec3{};
    }

    for (size_t index = 0u; index + 2u < indices.size(); index += 3u)
    {
        const uint32_t indexA = indices[index];
        const uint32_t indexB = indices[index + 1u];
        const uint32_t indexC = indices[index + 2u];

        if (indexA >= vertices.size()
            || indexB >= vertices.size()
            || indexC >= vertices.size())
        {
            continue;
        }

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
            vertex.Normal = { 0.0f, 0.0f, 1.0f };
            continue;
        }

        vertex.Normal.Normalize();
    }
}
} // namespace

SoftBodyClothDeformer::SoftBodyClothDeformer(uint32_t rows, uint32_t columns)
    : m_Rows(rows),
      m_Columns(columns)
{
    ph::SoftBodySolverSettings solverSettings{};

    // ClothはInternal Constraint・自己衝突・外部Colliderが互いに補正し合うため、
    // 統合XPBD Stepではこの反復回数が全Constraint共通の収束回数になります。
    solverSettings.SolverIterations = 12u;
    solverSettings.CollisionThickness = 0.005f;
    m_Solver.SetSettings(solverSettings);

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

void SoftBodyClothDeformer::SetCollisionPlane(const math::Vec3& normal, float offset)
{
    m_CollisionPlaneEnabled = true;
    m_CollisionPlaneNormal = normal;
    m_CollisionPlaneOffset = offset;

    if (m_Initialized)
    {
        ApplyCollisionPlaneToSolver();
    }
}

void SoftBodyClothDeformer::DisableCollisionPlane()
{
    m_CollisionPlaneEnabled = false;

    if (m_Initialized)
    {
        m_Solver.ClearPlaneColliders();
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

void SoftBodyClothDeformer::ApplyCollisionPlaneToSolver()
{
    m_Solver.ClearPlaneColliders();

    if (m_CollisionPlaneEnabled == false)
    {
        return;
    }

    m_CollisionPlaneIndex = m_Solver.AddPlaneCollider(
        m_CollisionPlaneNormal,
        m_CollisionPlaneOffset);
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

    ph::SoftBodyClothSettings clothSettings{};
    clothSettings.Rows = m_Rows;
    clothSettings.Columns = m_Columns;
    clothSettings.Width = 1.0f;
    clothSettings.Height = 1.0f;
    clothSettings.InverseMass = 1.0f;
    clothSettings.StructuralCompliance = 0.000001f;
    clothSettings.ShearCompliance = 0.000002f;
    clothSettings.BendingModel = ph::SoftBodyClothBendingModel::Dihedral;
    clothSettings.BendingCompliance = 0.00002f;
    clothSettings.PinTopLeft = true;
    clothSettings.PinTopRight = true;

    m_Solver.Clear();
    m_Cloth = ph::SoftBodyClothBuilder::Build(m_Solver, clothSettings);
    m_Initialized = true;

    ApplyCollisionSphereToSolver();
    ApplyCollisionPlaneToSolver();
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

    if (m_Rows == 0u || m_Columns == 0u)
    {
        return;
    }

    const float horizontalSpacing = 1.0f / static_cast<float>(m_Columns);
    const float verticalSpacing = 1.0f / static_cast<float>(m_Rows);
    const float minimumSpacing = std::min(horizontalSpacing, verticalSpacing);

    ph::SoftBodySelfCollisionSettings particleSettings{};
    particleSettings.Enabled = true;
    particleSettings.ParticleRadius = minimumSpacing * 0.15f;
    particleSettings.Compliance = 0.0f;

    ph::SoftBodyParticleTriangleSelfCollisionSettings particleTriangleSettings{};
    particleTriangleSettings.Enabled = true;
    particleTriangleSettings.Thickness = minimumSpacing * 0.30f;
    particleTriangleSettings.Compliance = 0.0f;

    // ========================================================================
    // Unified Cloth XPBD Step
    // ========================================================================
    // 旧実装:
    //   Solver::Step -> Particle-Particle後処理 -> Particle-Triangle後処理
    //
    // 現実装:
    //   [Distance -> Dihedral -> Particle-Particle -> Particle-Triangle
    //    -> Sphere -> Plane] x SolverIterations
    //
    // 全Constraintが同じ反復内で最新Positionを見られるため、外部Colliderへ押されたClothが
    // 自己貫通した場合も次iterationで直ちに再評価されます。またVelocity再構築は最後の1回だけです。
    m_Solver.StepWithSelfCollisions(
        deltaTime,
        m_Cloth,
        particleSettings,
        particleTriangleSettings);

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

    RecalculateClothNormals(deformedVertices, geometry->GetIndices());

    if (geometry->SetVertices(std::move(deformedVertices)) == false)
    {
        return;
    }

    mesh.SyncGeometry();
}

} // namespace Raven
