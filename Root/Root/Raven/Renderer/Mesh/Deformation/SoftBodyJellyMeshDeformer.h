#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Physics/SoftBody/SoftBodyJellySurface.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformer.h"

namespace Raven
{

class MeshGeometry;

namespace ph
{
class SoftBodySolver;
}

// ============================================================================
// Soft Body Jelly Mesh Deformer
// ============================================================================
// Physics側ですでに更新されたJelly Particle Positionを、Dynamic MeshGeometryへ同期するDeformerです。
//
// 重要:
// このクラス自身はPhysics Stepを進めません。
// Renderer更新からPhysicsを駆動するとSystem順序が逆転しやすいため、SimulationはPhysics側で先に
// StepSoftBodyJelly()を呼び、その後MeshDeformationSystemから本Deformerを更新する責務分離にします。
//
// Updateでは:
//   1. Surface Particle PositionをMesh Vertexへコピー
//   2. Surface Triangleから面積加重Vertex Normalを再計算
//   3. MeshGeometry::SetVertices()
//   4. Mesh::SyncGeometry()
//
// の順でCPU -> GPU同期まで行います。
class SoftBodyJellyMeshDeformer final : public MeshDeformer
{
public:
    SoftBodyJellyMeshDeformer(
        ph::SoftBodySolver& solver,
        ph::SoftBodyJellySurface surface,
        const math::Vec3& color = math::Vec3{ 0.35f, 0.85f, 0.55f });

    void Update(Mesh& mesh, float deltaTime) override;

    const ph::SoftBodyJellySurface& GetSurface() const
    {
        return m_Surface;
    }

    // Deformerが参照するSurface Topologyに対応したDynamic Geometryを生成します。
    // Positionは現在のParticle位置、IndexはSurface Triangleから構築し、Normalも初期計算します。
    Ref<MeshGeometry> CreateGeometry() const;

private:
    bool BuildVerticesAndIndices(
        std::vector<class MeshVertex>& outVertices,
        std::vector<uint32_t>& outIndices) const;

    void RecalculateNormals(
        std::vector<class MeshVertex>& vertices,
        const std::vector<uint32_t>& indices) const;

private:
    ph::SoftBodySolver* m_Solver = nullptr;
    ph::SoftBodyJellySurface m_Surface;
    math::Vec3 m_Color{ 0.35f, 0.85f, 0.55f };
};

} // namespace Raven
