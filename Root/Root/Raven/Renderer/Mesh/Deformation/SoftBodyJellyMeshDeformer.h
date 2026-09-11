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
// SoftBodyClothDeformerと同じく、JellyのPhysics StateとMesh変形を橋渡しするDeformerです。
//
// Constructor:
//   Jelly格子 / Tetrahedron / Volume Constraint / Surface Topologyを構築します。
//
// CreateGeometry():
//   現在のSurface ParticleからDynamic MeshGeometryを生成します。
//
// 従来のUpdate():
//   1. StepSoftBodyJelly()でXPBD Simulationを進める
//   2. Surface Particle PositionをMesh Vertexへコピー
//   3. Surface Triangleから面積加重Vertex Normalを再計算
//   4. MeshGeometry::SetVertices()
//   5. Mesh::SyncGeometry()
//
// 現在は上記責務をSimulate()とSynchronizeMesh()へ分離しています。
// これによりSimulationだけをPhysics Fixed Stepへ移管しても、Renderer側の頂点同期処理を
// そのまま再利用できます。互換Update()は両処理を連続実行するため、直接利用時の契約も維持します。
class SoftBodyJellyMeshDeformer final : public MeshDeformer
{
public:
    explicit SoftBodyJellyMeshDeformer(
        const ph::SoftBodyJellySettings& settings = ph::SoftBodyJellySettings{},
        const math::Vec3& color = math::Vec3{ 0.35f, 0.85f, 0.55f });

    // 互換UpdateはSimulation -> Mesh同期を連続実行します。
    void Update(Mesh& mesh, float deltaTime) override;

    // Physics Stateだけを進めます。Renderer / Meshには触れません。
    void Simulate(float deltaTime);

    // 現在のParticle PositionをMeshへ反映します。Physics Stateは変更しません。
    void SynchronizeMesh(Mesh& mesh);

    ph::SoftBodySolver* GetSoftBodySolver() override { return &m_Solver; }

    // MeshDeformer共通境界へJellyの分離済み処理を接続します。
    // Fixed Step側は具体型へのdowncastを行わず、この共通境界だけを使用します。
    bool HasSeparatedSoftBodyUpdate() const override { return true; }
    void SimulateSoftBody(float deltaTime) override { Simulate(deltaTime); }
    void SynchronizeSoftBodyMesh(Mesh& mesh) override { SynchronizeMesh(mesh); }

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

    ph::SoftBodySolver m_Solver;
    ph::SoftBodyJelly m_Jelly;
    ph::SoftBodyJellySurface m_Surface;
};

} // namespace Raven
