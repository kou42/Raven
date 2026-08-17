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
// Scene側はMeshDeformationSystem::Update()を呼ぶだけで、物理更新とGPU同期まで進みます。
class SoftBodyClothDeformer : public MeshDeformer
{
public:
    SoftBodyClothDeformer(uint32_t rows, uint32_t columns);

    void Update(Mesh& mesh, float deltaTime) override;

    ph::SoftBodySolver& GetSolver() { return m_Solver; }
    const ph::SoftBodySolver& GetSolver() const { return m_Solver; }

private:
    bool InitializeFromMesh(Mesh& mesh);

private:
    uint32_t m_Rows = 0u;
    uint32_t m_Columns = 0u;
    bool m_Initialized = false;
    ph::SoftBodySolver m_Solver;
    ph::SoftBodyCloth m_Cloth;
};

} // namespace Raven
