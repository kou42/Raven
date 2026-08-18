#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Physics/SoftBody/SoftBodyParticle.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// XPBD Tetrahedron Volume Constraint
// ============================================================================
// 4 Particleで作る四面体の「符号付き体積」をRestVolumeへ保つXPBD Constraintです。
//
// ClothのDistance / Dihedral Constraintは主に面の伸び・曲げを制御しますが、
// ゼリーのような3次元SoftBodyでは「内部体積を失わない」Constraintが必要になります。
// Volume ConstraintをDistance Constraintと同じSolver iteration内で解くことで、
// 外形は柔らかく変形しつつ、押し潰された際には元の体積へ戻ろうとする挙動を作れます。
//
// 符号付き体積:
//
//   V = dot(p1 - p0, cross(p2 - p0, p3 - p0)) / 6
//
// RestVolumeも符号付きで保持します。絶対値へ変換してしまうと四面体が反転した場合に
// 裏返った状態も正しい体積として扱ってしまうため、Topologyの向きを保持する目的で
// 符号を残しています。
struct XPBDVolumeConstraint
{
    uint32_t Particle0 = 0u;
    uint32_t Particle1 = 0u;
    uint32_t Particle2 = 0u;
    uint32_t Particle3 = 0u;

    float RestVolume = 0.0f;

    // 0.0fに近いほど非圧縮に近くなります。
    // ゼリーではDistanceよりVolumeを硬めに設定すると、体積を保ちながら外形だけが
    // 柔らかく変形する挙動を作りやすくなります。
    float Compliance = 0.0f;

    // 同一Step内のXPBD iteration間で蓄積するLagrange multiplierです。
    // Step開始時に0へ戻し、現在はWarm Startを行いません。
    float Lambda = 0.0f;
};

// 4 Particleの現在位置からRestVolumeを計算してConstraintを構築します。
// 退化した四面体も構築自体は可能ですが、Solver側では勾配がほぼ0の場合に安全にskipします。
XPBDVolumeConstraint CreateXPBDVolumeConstraint(
    const std::vector<SoftBodyParticle>& particles,
    uint32_t particle0,
    uint32_t particle1,
    uint32_t particle2,
    uint32_t particle3,
    float compliance = 0.0f);

// デバッグ・テスト・Builderから共通利用できるよう、符号付き四面体体積の計算を公開します。
float ComputeSignedTetrahedronVolume(
    const math::Vec3& p0,
    const math::Vec3& p1,
    const math::Vec3& p2,
    const math::Vec3& p3);

} // namespace ph
} // namespace Raven
