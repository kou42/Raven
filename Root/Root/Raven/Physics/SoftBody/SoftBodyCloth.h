#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/SoftBody/SoftBodySolver.h"

namespace Raven
{
namespace ph
{

struct SoftBodyClothSettings
{
    uint32_t Rows = 16u;
    uint32_t Columns = 16u;
    float Width = 1.0f;
    float Height = 1.0f;
    float InverseMass = 1.0f;
    float StructuralCompliance = 0.0f;
    float ShearCompliance = 0.0f;
    bool PinTopLeft = true;
    bool PinTopRight = true;
};

// Cloth自体はParticle/Constraintを所有せず、SoftBodySolverへ登録されたIndexだけを保持します。
// これによりSolverを将来複数SoftBodyで共有しても、Cloth側の責務をTopology管理に限定できます。
struct SoftBodyCloth
{
    uint32_t Rows = 0u;
    uint32_t Columns = 0u;
    std::vector<uint32_t> ParticleIndices;

    uint32_t GetParticleIndex(uint32_t row, uint32_t column) const
    {
        return ParticleIndices[static_cast<size_t>(row) * static_cast<size_t>(Columns + 1u) + column];
    }
};

class SoftBodyClothBuilder
{
public:
    // 原点中心のXY平面へClothを生成します。
    // row=0を上端として、PinTopLeft/PinTopRightは左右上端のParticleを固定します。
    static SoftBodyCloth Build(SoftBodySolver& solver, const SoftBodyClothSettings& settings);
};

} // namespace ph
} // namespace Raven
