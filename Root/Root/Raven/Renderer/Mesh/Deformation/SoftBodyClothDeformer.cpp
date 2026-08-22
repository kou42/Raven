#include "Raven/Renderer/Mesh/Deformation/SoftBodyClothDeformer.h"

#include <algorithm>
#include <vector>

#include "Raven/Core/CPUProfiler.h"
#include "Raven/Physics/SoftBody/SoftBodyParticleTriangleSelfCollision.h"
#include "Raven/Physics/SoftBody/SoftBodySelfCollision.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven
{
namespace
{
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
    solverSettings.SolverIterations = 12u;
    solverSettings.CollisionThickness = 0.005f;
    m_Solver.SetSettings(solverSettings);

    // 0.04 / 0.05 / 0.06を同一Solver状態から比較した結果、0.06が最短だったため
    // 通常実行時の初期値にもLargeを採用します。
    m_ParticleTriangleSpatialHashCellSize =
        ph::SoftBodyParticleTriangleSelfCollisionSettings::SpatialHashCellSizeLarge;

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

void SoftBodyClothDeformer::SetParticleTriangleSpatialHashCellSize(float cellSize)
{
    m_ParticleTriangleSpatialHashCellSize = std::max(cellSize, 1.0e-4f);
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
    RAVEN_PROFILE_SCOPE("SoftBody.Cloth.Initialize");

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
    RAVEN_PROFILE_SCOPE("SoftBody.Cloth.Update");

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
    particleTriangleSettings.SpatialHashCellSize = m_ParticleTriangleSpatialHashCellSize;
    particleTriangleSettings.Compliance = 0.0f;

    {
        RAVEN_PROFILE_SCOPE("SoftBody.Cloth.Solver");
        m_Solver.StepWithSelfCollisions(
            deltaTime,
            m_Cloth,
            particleSettings,
            particleTriangleSettings);
    }

    // ========================================================================
    // Particle-Triangle Spatial Hash / NarrowPhase Funnel Counters
    // ========================================================================
    // Constraint後半を
    //   Constraint -> DenominatorReject / DeltaLambda -> DeltaLambdaReject / PositionCorrection
    // まで分解して記録します。
    //
    // DenominatorRejectが多い場合は固定Particle構成など、そもそも解けないConstraintをより前段で
    // 除外できる可能性があります。DeltaLambdaRejectが多い場合はXPBD式まで評価しても実Position更新へ
    // 寄与しない候補が多いため、Lambda状態を利用したcheap rejectが次の最適化候補になります。
    const ph::SoftBodyParticleTriangleCollisionStatistics& particleTriangleStatistics =
        m_Solver.GetParticleTriangleCollisionStatistics();

    CPUProfiler::Get().AddCounter(
        "SoftBody.ParticleTriangle.SpatialHashCellSize",
        static_cast<double>(m_ParticleTriangleSpatialHashCellSize));

    CPUProfiler::Get().AddCounter(
        "SoftBody.ParticleTriangle.Funnel.Candidate",
        static_cast<double>(particleTriangleStatistics.CandidateCount));
    CPUProfiler::Get().AddCounter(
        "SoftBody.ParticleTriangle.Funnel.NarrowPhase",
        static_cast<double>(particleTriangleStatistics.NarrowPhaseCount));
    CPUProfiler::Get().AddCounter(
        "SoftBody.ParticleTriangle.Funnel.Distance",
        static_cast<double>(particleTriangleStatistics.DistanceCount));
    CPUProfiler::Get().AddCounter(
        "SoftBody.ParticleTriangle.Funnel.Constraint",
        static_cast<double>(particleTriangleStatistics.ConstraintCount));
    CPUProfiler::Get().AddCounter(
        "SoftBody.ParticleTriangle.Funnel.DenominatorReject",
        static_cast<double>(particleTriangleStatistics.DenominatorRejectCount));
    CPUProfiler::Get().AddCounter(
        "SoftBody.ParticleTriangle.Funnel.DeltaLambda",
        static_cast<double>(particleTriangleStatistics.DeltaLambdaCount));
    CPUProfiler::Get().AddCounter(
        "SoftBody.ParticleTriangle.Funnel.DeltaLambdaReject",
        static_cast<double>(particleTriangleStatistics.DeltaLambdaRejectCount));
    CPUProfiler::Get().AddCounter(
        "SoftBody.ParticleTriangle.Funnel.PositionCorrection",
        static_cast<double>(particleTriangleStatistics.PositionCorrectionCount));

    // Constraintへ到達した候補のうち、どの程度が実Position補正まで残ったかを比率でも表示します。
    // 絶対件数と合わせて見ることで、シーン規模が変わっても後半funnelの効率を比較できます。
    double positionCorrectionRatio = 0.0;
    if (particleTriangleStatistics.ConstraintCount > 0u)
    {
        positionCorrectionRatio =
            static_cast<double>(particleTriangleStatistics.PositionCorrectionCount)
            / static_cast<double>(particleTriangleStatistics.ConstraintCount);
    }
    CPUProfiler::Get().AddCounter(
        "SoftBody.ParticleTriangle.Funnel.PositionCorrectionRatio",
        positionCorrectionRatio);

    // 既存のCounter名はCell Size比較結果との互換性を維持するため残します。
    CPUProfiler::Get().AddCounter(
        "SoftBody.ParticleTriangle.CandidateCount",
        static_cast<double>(particleTriangleStatistics.CandidateCount));
    CPUProfiler::Get().AddCounter(
        "SoftBody.ParticleTriangle.NarrowPhaseCount",
        static_cast<double>(particleTriangleStatistics.NarrowPhaseCount));

    double narrowPhaseRatio = 0.0;
    if (particleTriangleStatistics.CandidateCount > 0u)
    {
        narrowPhaseRatio =
            static_cast<double>(particleTriangleStatistics.NarrowPhaseCount)
            / static_cast<double>(particleTriangleStatistics.CandidateCount);
    }
    CPUProfiler::Get().AddCounter(
        "SoftBody.ParticleTriangle.NarrowPhaseRatio",
        narrowPhaseRatio);

    const std::vector<ph::SoftBodyParticle>& particles = m_Solver.GetParticles();
    const std::vector<MeshVertex>& sourceVertices = geometry->GetVertices();
    if (sourceVertices.size() != m_Cloth.ParticleIndices.size())
    {
        return;
    }

    std::vector<MeshVertex> deformedVertices;
    {
        RAVEN_PROFILE_SCOPE("SoftBody.Cloth.MeshPositions");
        deformedVertices = sourceVertices;
        for (size_t vertexIndex = 0u; vertexIndex < deformedVertices.size(); ++vertexIndex)
        {
            const uint32_t particleIndex = m_Cloth.ParticleIndices[vertexIndex];
            if (particleIndex >= particles.size())
            {
                return;
            }

            deformedVertices[vertexIndex].Position = particles[particleIndex].Position;
        }
    }

    {
        RAVEN_PROFILE_SCOPE("SoftBody.Cloth.Normals");
        RecalculateClothNormals(deformedVertices, geometry->GetIndices());
    }

    {
        RAVEN_PROFILE_SCOPE("SoftBody.Cloth.SetVertices");
        if (geometry->SetVertices(std::move(deformedVertices)) == false)
        {
            return;
        }
    }

    {
        RAVEN_PROFILE_SCOPE("SoftBody.Cloth.GPUSync");
        mesh.SyncGeometry();
    }
}

} // namespace Raven