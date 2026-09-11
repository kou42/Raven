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
// JellyのPhysics StateとMesh表示を橋渡しするDeformerです。
// SimulationとMesh同期を分離しておき、後続でSimulationだけをPhysics Fixed Stepへ
// 移管してもRenderer側の頂点同期処理を再利用できる構造にします。
class SoftBodyJellyMeshDeformer final : public MeshDeformer
{
public:
    explicit SoftBodyJellyMeshDeformer(
        const ph::SoftBodyJellySettings& settings = ph::SoftBodyJellySettings{},
        const math::Vec3& color = math::Vec3{ 0.35f, 0.85f, 0.55f });

    // 互換Updateは現段階ではSimulation -> Mesh同期を連続実行します。
    void Update(Mesh& mesh, float deltaTime) override;

    // Physics Stateだけを進めます。Renderer / Meshには触れません。
    void Simulate(float deltaTime);

    // 現在のParticle PositionをMeshへ反映します。Physics Stateは変更しません。
    void SynchronizeMesh(Mesh& mesh);

    ph::SoftBodySolver* GetSoftBodySolver() override { return &m_Solver; }

    // MeshDeformer共通境界へJellyの分離済み処理を接続します。
    // この段階では呼び出し元を切り替えず、Fixed Step移管時に具体型downcastを不要にします。
    bool HasSeparatedSoftBodyUpdate() const override { return true; }
    void SimulateSoftBody(float deltaTime) override { Simulate(deltaTime); }
    void SynchronizeSoftBodyMesh(Mesh& mesh) override { SynchronizeMesh(mesh); }

    Ref<MeshGeometry> CreateGeometry() const;

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
