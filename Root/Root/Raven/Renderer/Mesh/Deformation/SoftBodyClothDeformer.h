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

    // デバッグや将来の連成処理用の参照です。Solverの所有権はDeformerが保持します。
    ph::SoftBodySolver& GetSolver() { return m_Solver; }
    const ph::SoftBodySolver& GetSolver() const { return m_Solver; }

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

    ph::SoftBodySolver m_Solver;
    ph::SoftBodyCloth m_Cloth;
};

} // namespace Raven
