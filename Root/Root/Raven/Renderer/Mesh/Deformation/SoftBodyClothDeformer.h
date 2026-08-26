#pragma once

#include <cstdint>

#include "Raven/Physics/SoftBody/SoftBodyCloth.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformer.h"

namespace Raven
{

// ============================================================================
// SoftBodyClothDeformer
// ============================================================================
// Dynamic Gridの頂点とXPBD Cloth Particleを1対1で対応させるMeshDeformerです。
// Scene側はSoftBodyの具体的なSolver処理を知る必要がなく、既存MeshDeformationSystemから
// Update()を呼ぶだけで物理更新、頂点反映、法線再計算、GPU同期まで進みます。
//
// 現段階の物理計算はClothローカル空間で完結します。World-spaceのRigidBodyとの双方向連成は
// このDeformerへ直接混ぜず、後続の連成レイヤーからSolverへCollider情報を渡す想定です。
class SoftBodyClothDeformer : public MeshDeformer
{
public:
    SoftBodyClothDeformer(uint32_t rows, uint32_t columns);

    // 初回はDynamic GridからClothを構築し、以降はSolver更新結果をMeshへ同期します。
    void Update(Mesh& mesh, float deltaTime) override;

    // Clothローカル空間上の静的Sphere Colliderを設定します。
    // 初期化後に変更された場合もSolverへ設定を再登録します。
    void SetCollisionSphere(const math::Vec3& center, float radius);
    void DisableCollisionSphere();

    // Clothローカル空間のPlane Colliderです。
    // Plane式は dot(normal, x) - offset = 0 とし、normal側をClothが存在できる外側とします。
    void SetCollisionPlane(const math::Vec3& normal, float offset);
    void DisableCollisionPlane();

    // Particle-Triangle Broad PhaseのCell Size比較用設定です。
    // 0以下はSpatial Hash側の最小値へ丸められますが、通常はSettingsに定義した
    // 0.04 / 0.05 / 0.06の比較プリセットを使用します。
    void SetParticleTriangleSpatialHashCellSize(float cellSize);
    float GetParticleTriangleSpatialHashCellSize() const
    {
        return m_ParticleTriangleSpatialHashCellSize;
    }

    // ========================================================================
    // Solver Temporary Allocator Before / After Comparison
    // ========================================================================
    // ④の計測ではCloth構成やSolverIterationsを変えず、Temporary Allocator Modeだけを
    // Heap / FrameAllocatorで切り替えて比較します。
    //
    // GetSettings()の非const参照へ直接Modeを書き込むと、SolverTemporaryAllocationStatistics側の
    // Backing Allocator切替処理を通らず、設定値と実際の確保元が不一致になる可能性があります。
    // 必ずSetSettings()を経由するこのAPIを使い、比較条件とBackingを同時に切り替えます。
    void SetTemporaryAllocatorMode(ph::SoftBodyTemporaryAllocatorMode mode)
    {
        ph::SoftBodySolverSettings settings = m_Solver.GetSettings();
        settings.TemporaryAllocatorMode = mode;
        m_Solver.SetSettings(settings);
    }

    ph::SoftBodyTemporaryAllocatorMode GetTemporaryAllocatorMode() const
    {
        return m_Solver.GetSettings().TemporaryAllocatorMode;
    }

    // デバッグや将来の連成処理用の参照です。Solverの所有権はDeformerが保持します。
    ph::SoftBodySolver& GetSolver() { return m_Solver; }
    const ph::SoftBodySolver& GetSolver() const { return m_Solver; }

    // Browser Debug Writerなど、PhysicsのParticle Indexへ変換したTopologyが必要な
    // デバッグ処理向けの読み取り専用参照です。Clothの所有権と変更責務はDeformerに残します。
    const ph::SoftBodyCloth& GetCloth() const { return m_Cloth; }

private:
    // Dynamic Gridの頂点数を検証し、同じrow-major順でCloth Particleを生成します。
    bool InitializeFromMesh(Mesh& mesh);

    // Solver::Clear()後にもCollider設定を復元できるよう、保持値から再登録します。
    void ApplyCollisionSphereToSolver();
    void ApplyCollisionPlaneToSolver();

private:
    uint32_t m_Rows = 0u;
    uint32_t m_Columns = 0u;
    bool m_Initialized = false;

    // Collider設定はDeformer側にも保持し、Cloth再構築後にSolverへ復元します。
    bool m_CollisionSphereEnabled = false;
    math::Vec3 m_CollisionSphereCenter{};
    float m_CollisionSphereRadius = 0.0f;
    uint32_t m_CollisionSphereIndex = 0u;

    bool m_CollisionPlaneEnabled = false;
    math::Vec3 m_CollisionPlaneNormal{ 0.0f, 1.0f, 0.0f };
    float m_CollisionPlaneOffset = 0.0f;
    uint32_t m_CollisionPlaneIndex = 0u;

    // 0.04 / 0.05 / 0.06比較で最短だった0.06を通常デフォルトにします。
    // RuntimeからSetterで変更してもClothを再構築せず、次StepからBroad Phaseだけ差し替えられます。
    float m_ParticleTriangleSpatialHashCellSize = 0.06f;

    ph::SoftBodySolver m_Solver;
    ph::SoftBodyCloth m_Cloth;
};

} // namespace Raven
