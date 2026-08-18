#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/SoftBody/SoftBodySolver.h"
#include "Raven/Physics/SoftBody/XPBDVolumeConstraint.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// Soft Body Jelly Settings
// ============================================================================
// 直方体領域を規則格子へ分割し、各Cube Cellを6個のTetrahedronへ分解して
// 体積SoftBodyを構築するための設定です。
//
// CellsX/Y/Zは「Particle数」ではなくCell数です。
// したがって各軸のParticle数は Cells + 1 になります。
struct SoftBodyJellySettings
{
    uint32_t CellsX = 2u;
    uint32_t CellsY = 2u;
    uint32_t CellsZ = 2u;

    float Width = 1.0f;
    float Height = 1.0f;
    float Depth = 1.0f;

    // 全Particleの共通逆質量です。
    // 0.0fを指定すると全点固定になるため、通常のゼリーでは1.0f前後を使用します。
    float InverseMass = 1.0f;

    // Tetrahedronを構成する全Edgeへ張るDistance ConstraintのComplianceです。
    // Volumeだけでは四面体が形を変えながら同じ体積を保てるため、Distance Constraintを併用して
    // せん断・過度な伸縮を抑えます。
    float DistanceCompliance = 0.0001f;

    // 各Tetrahedronの符号付き体積をRestVolumeへ保つComplianceです。
    // Distanceより小さめにして体積を硬くすると、外形は変形するが内部体積は失いにくい
    // ゼリーらしい挙動になります。
    float VolumeCompliance = 0.000001f;

    // Step後にJelly Particleへ適用するVelocity dampingです。
    // 0.0fなら減衰なし、1.0fならそのStepで速度を完全に失います。
    // 60Hz基準で時間刻み補正を行うため、dtが変わっても減衰感が極端に変わりにくくします。
    float VelocityDamping = 0.02f;
};

// 1つのTetrahedronを構成するParticle Indexです。
// VolumeConstraintと同じ4頂点を保持しますが、Topology情報として独立して残すことで、
// 後の表面Triangle抽出・デバッグ描画・任意Mesh追従に利用できます。
struct SoftBodyTetrahedron
{
    uint32_t Particle0 = 0u;
    uint32_t Particle1 = 0u;
    uint32_t Particle2 = 0u;
    uint32_t Particle3 = 0u;
};

// ============================================================================
// Soft Body Jelly Topology
// ============================================================================
// Particle / Distance Constraint自体は既存方針に合わせてSoftBodySolverが所有します。
// Jelly側は格子Particle Index、Tetrahedron Topology、Volume Constraintを保持します。
//
// Volume ConstraintはStep中にLambdaを書き換えるためJelly側で所有し、
// StepSoftBodyJelly()からsolver.StepWithVolumeConstraints()へ渡します。
struct SoftBodyJelly
{
    uint32_t CellsX = 0u;
    uint32_t CellsY = 0u;
    uint32_t CellsZ = 0u;

    float VelocityDamping = 0.02f;

    std::vector<uint32_t> ParticleIndices;
    std::vector<SoftBodyTetrahedron> Tetrahedra;
    std::vector<XPBDVolumeConstraint> VolumeConstraints;

    uint32_t GetParticleIndex(uint32_t x, uint32_t y, uint32_t z) const
    {
        const std::size_t vertexCountX = static_cast<std::size_t>(CellsX + 1u);
        const std::size_t vertexCountY = static_cast<std::size_t>(CellsY + 1u);
        const std::size_t index =
            static_cast<std::size_t>(z) * vertexCountY * vertexCountX
            + static_cast<std::size_t>(y) * vertexCountX
            + static_cast<std::size_t>(x);
        return ParticleIndices[index];
    }
};

class SoftBodyJellyBuilder
{
public:
    // 原点中心の直方体ゼリーを構築します。
    // 各Cube Cellは常に local vertex 000 -> 111 の体対角線を共有する6個のTetrahedronへ
    // 分割します。全Cellで同じ規則を使用するため、隣接Cell境界でも三角形分割が一致します。
    static SoftBodyJelly Build(SoftBodySolver& solver, const SoftBodyJellySettings& settings);
};

// Jelly用の1 Simulation Stepです。
// 既存SoftBodySolverのVolume統合Stepを進めた後、Jelly Particleだけへ速度減衰を適用します。
// Solver全体へ減衰を掛けないため、将来ClothとJellyが同じSolverへ登録された場合でも
// Jelly Material設定がCloth Particleへ漏れません。
void StepSoftBodyJelly(SoftBodySolver& solver, SoftBodyJelly& jelly, float deltaTime);

} // namespace ph
} // namespace Raven
