#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/SoftBody/SoftBodySolver.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// Cloth Bending Model
// ============================================================================
// Distance:
//   1頂点飛ばしのParticle間距離で曲げを近似する旧方式です。
//   安価で実装も単純なので比較・フォールバック用途として残します。
//
// Dihedral:
//   共有Edgeを持つ2Triangle間の二面角を直接拘束します。
//   Topology上の「折れ角」そのものを扱うため、ClothのBendingモデルとしてはこちらを既定にします。
enum class SoftBodyClothBendingModel
{
    Distance,
    Dihedral
};

struct SoftBodyClothSettings
{
    uint32_t Rows = 16u;
    uint32_t Columns = 16u;
    float Width = 1.0f;
    float Height = 1.0f;
    float InverseMass = 1.0f;
    float StructuralCompliance = 0.0f;
    float ShearCompliance = 0.0f;

    // ========================================================================
    // Bending Compliance
    // ========================================================================
    // BendingModelによって意味するConstraint形状は変わりますが、Complianceの意味は共通です。
    // 0に近いほど硬く、値を大きくするほど折れ曲がりやすくなります。
    // XPBDなので alphaTilde = BendingCompliance / dt^2 として時間刻み依存を抑えます。
    float BendingCompliance = 0.00002f;

    // 既定は隣接Triangle間の二面角を直接拘束するDihedral Bendingです。
    // 旧Distance Bendingと挙動を比較したい場合はDistanceへ切り替えられます。
    SoftBodyClothBendingModel BendingModel = SoftBodyClothBendingModel::Dihedral;

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
