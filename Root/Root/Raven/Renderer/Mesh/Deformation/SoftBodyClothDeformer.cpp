#include "Raven/Renderer/Mesh/Deformation/SoftBodyClothDeformer.h"

#include <algorithm>
#include <vector>

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
    // 前フレームのNormalを残すと蓄積してしまうため、毎回ゼロから作り直します。
    for (MeshVertex& vertex : vertices)
    {
        vertex.Normal = math::Vec3{};
    }

    for (size_t index = 0u; index + 2u < indices.size(); index += 3u)
    {
        const uint32_t indexA = indices[index];
        const uint32_t indexB = indices[index + 1u];
        const uint32_t indexC = indices[index + 2u];

        // Dynamic Topologyはまだ扱わない設計ですが、壊れたIndexからメモリ外参照しないよう防御します。
        if (indexA >= vertices.size()
            || indexB >= vertices.size()
            || indexC >= vertices.size())
        {
            continue;
        }

        const math::Vec3 edgeAB = vertices[indexB].Position - vertices[indexA].Position;
        const math::Vec3 edgeAC = vertices[indexC].Position - vertices[indexA].Position;
        const math::Vec3 faceNormal = math::Vec3::Cross(edgeAB, edgeAC);

        // 面積0のTriangleはNormalを定義できないため寄与させません。
        if (faceNormal.LengthSq() <= math::Epsilon * math::Epsilon)
        {
            continue;
        }

        // 面法線を正規化せず加算することで、面積の大きいTriangleほど強く寄与する
        // area-weighted vertex normalになります。
        vertices[indexA].Normal += faceNormal;
        vertices[indexB].Normal += faceNormal;
        vertices[indexC].Normal += faceNormal;
    }

    for (MeshVertex& vertex : vertices)
    {
        if (vertex.Normal.LengthSq() <= math::Epsilon * math::Epsilon)
        {
            // ClothはXY平面を基準に生成するため、退化時の安全なfallbackを+Zにします。
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

    // Clothは多数のInternal / Collision Constraintが互いに影響するため、
    // RigidBody Contactより多めの反復を使います。まず12回を目視確認用の基準値とし、
    // 後から品質設定へ外出しできる構成にしています。
    solverSettings.SolverIterations = 12u;
    solverSettings.CollisionThickness = 0.005f;
    m_Solver.SetSettings(solverSettings);

    // 最初の目視確認用にCloth中央より少し下へ静的Sphereを置きます。
    // Scene側からSetCollisionSphere()を呼べば任意の位置・半径へ差し替えられます。
    SetCollisionSphere({ 0.0f, -0.12f, 0.0f }, 0.20f);
}

void SoftBodyClothDeformer::SetCollisionSphere(const math::Vec3& center, float radius)
{
    // radius <= 0.0f は無効Colliderとして扱い、負の半径がSolverへ入らないようclampします。
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
    // Normalの正規化とゼロベクトルfallbackはSolver側へ集約します。
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
    // 現段階ではデモ用Sphereを1個だけ管理するため、一度全削除して設定値から再登録します。
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
    // Sphereと同じく、現在はDeformerが1枚の床Planeを管理する最小構成です。
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

    // CreateDynamicGrid(rows, columns)は(rows+1)*(columns+1)頂点をrow-majorで生成します。
    // Cloth Builderも同じ順序を使うため、頂点数が一致すればParticleとMesh頂点を1対1対応できます。
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

    // Structural: 縦横の伸びを抑える主要Constraint。
    clothSettings.StructuralCompliance = 0.000001f;

    // Shear: Quadが菱形へ潰れる変形を抑える対角Constraint。
    clothSettings.ShearCompliance = 0.000002f;

    // Bendingは隣接Triangle間の二面角を直接拘束するDihedralモデルを使用します。
    // 旧1頂点飛ばしDistance BendingもBuilder側に比較用として残しているため、
    // BendingModelをDistanceへ変更すれば挙動差を確認できます。
    clothSettings.BendingModel = ph::SoftBodyClothBendingModel::Dihedral;
    clothSettings.BendingCompliance = 0.00002f;
    clothSettings.PinTopLeft = true;
    clothSettings.PinTopRight = true;

    m_Solver.Clear();
    m_Cloth = ph::SoftBodyClothBuilder::Build(m_Solver, clothSettings);
    m_Initialized = true;

    // Solver::Clear()はColliderも消すため、Cloth再初期化後に現在のCollider設定を再登録します。
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

    // 物理側を先に進め、その最終Particle Positionを描画用Meshへ反映します。
    m_Solver.Step(deltaTime);

    const std::vector<ph::SoftBodyParticle>& particles = m_Solver.GetParticles();
    const std::vector<MeshVertex>& sourceVertices = geometry->GetVertices();
    if (sourceVertices.size() != m_Cloth.ParticleIndices.size())
    {
        return;
    }

    // Color / TexCoord等の属性は既存値を保持し、PositionとNormalだけを更新します。
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

    // Cloth変形後は生成時Normalが無効になるため、Fixed TopologyのIndexから毎フレーム再構築します。
    RecalculateClothNormals(deformedVertices, geometry->GetIndices());

    if (geometry->SetVertices(std::move(deformedVertices)) == false)
    {
        return;
    }

    // Geometry Revisionが変化した場合だけMesh側がVBOを更新します。
    mesh.SyncGeometry();
}

} // namespace Raven
