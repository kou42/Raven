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
    // 呼び出し側の挙動を変えず、次PhaseでFixed Stepへ移管できる境界だけを先に作ります。
    void Update(Mesh& mesh, float deltaTime) override;

    // Physics Stateだけを進めます。Renderer / Meshには触れません。
    void Simulate(float deltaTime);

    // 現在のParticle PositionをMeshへ反映します。Physics Stateは変更しません。
    void SynchronizeMesh(Mesh& mesh);

    // MeshDeformationSystemが具体的なJelly型を知らずにSoftBodyWorldへ登録するための境界です。
    // Registryは非所有なので、Solverのlifetimeは従来どおりDeformerが管理します。
    ph::SoftBodySolver* GetSoftBodySolver() override { return &m_Solver; }

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
