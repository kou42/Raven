#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Physics/SoftBody/SoftBodyJelly.h"
#include "Raven/Physics/SoftBody/SoftBodyJellySurface.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformer.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven
{

// ============================================================================
// Soft Body Jelly Mesh Deformer
// ============================================================================
// SoftBodyClothDeformerと同じく、JellyのPhysics StateとMesh変形を1つにまとめるDeformerです。
//
// Constructor:
//   Jelly格子 / Tetrahedron / Volume Constraint / Surface Topologyを構築します。
//
// CreateGeometry():
//   現在のSurface ParticleからDynamic MeshGeometryを生成します。
//
// Update():
//   1. StepSoftBodyJelly()でXPBD Simulationを進める
//   2. Surface Particle PositionをMesh Vertexへコピー
//   3. Surface Triangleから面積加重Vertex Normalを再計算
//   4. MeshGeometry::SetVertices()
//   5. Mesh::SyncGeometry()
//
// Scene側は既存MeshDeformationSystemからUpdate()を呼ぶだけで、Clothと同じ使用感で
// Jelly Simulation + Renderingを進められます。
class SoftBodyJellyMeshDeformer final : public MeshDeformer
{
public:
    explicit SoftBodyJellyMeshDeformer(
        const ph::SoftBodyJellySettings& settings = ph::SoftBodyJellySettings{},
        const math::Vec3& color = math::Vec3{ 0.35f, 0.85f, 0.55f });

    void Update(Mesh& mesh, float deltaTime) override;

    // Deformerが保持するSurface Topologyに対応したDynamic Geometryを生成します。
    Ref<MeshGeometry> CreateGeometry() const;

    // Jellyローカル空間上のPlane Colliderを設定します。
    // dot(normal, x) = offset をPlaneとし、normal側をParticleが存在できる側とします。
    void SetCollisionPlane(const math::Vec3& normal, float offset);
    void DisableCollisionPlane();

    ph::SoftBodySolver& GetSolver() { return m_Solver; }
    const ph::SoftBodySolver& GetSolver() const { return m_Solver; }

    ph::SoftBodyJelly& GetJelly() { return m_Jelly; }
    const ph::SoftBodyJelly& GetJelly() const { return m_Jelly; }

    const ph::SoftBodyJellySurface& GetSurface() const { return m_Surface; }

private:
    bool BuildVerticesAndIndices(
        std::vector<MeshVertex>& outVertices,
        std::vector<uint32_t>& outIndices) const;

    void RecalculateNormals(
        std::vector<MeshVertex>& vertices,
        const std::vector<uint32_t>& indices) const;

private:
    math::Vec3 m_Color{ 0.35f, 0.85f, 0.55f };

    bool m_CollisionPlaneEnabled = false;
    math::Vec3 m_CollisionPlaneNormal{ 0.0f, 1.0f, 0.0f };
    float m_CollisionPlaneOffset = 0.0f;

    ph::SoftBodySolver m_Solver;
    ph::SoftBodyJelly m_Jelly;
    ph::SoftBodyJellySurface m_Surface;
};

} // namespace Raven
