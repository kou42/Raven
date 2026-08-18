#include "Raven/Physics/SoftBody/XPBDVolumeConstraint.h"

#include <algorithm>
#include <cassert>

namespace Raven
{
namespace ph
{

float ComputeSignedTetrahedronVolume(
    const math::Vec3& p0,
    const math::Vec3& p1,
    const math::Vec3& p2,
    const math::Vec3& p3)
{
    // Scalar triple product / 6 が四面体の符号付き体積です。
    // 頂点順序が反転すると符号も反転するため、Topologyの向きも同時に表現できます。
    return math::Vec3::Dot(
        p1 - p0,
        math::Vec3::Cross(p2 - p0, p3 - p0)) / 6.0f;
}

XPBDVolumeConstraint CreateXPBDVolumeConstraint(
    const std::vector<SoftBodyParticle>& particles,
    uint32_t particle0,
    uint32_t particle1,
    uint32_t particle2,
    uint32_t particle3,
    float compliance)
{
    assert(particle0 < particles.size());
    assert(particle1 < particles.size());
    assert(particle2 < particles.size());
    assert(particle3 < particles.size());

    assert(particle0 != particle1);
    assert(particle0 != particle2);
    assert(particle0 != particle3);
    assert(particle1 != particle2);
    assert(particle1 != particle3);
    assert(particle2 != particle3);

    XPBDVolumeConstraint constraint{};
    constraint.Particle0 = particle0;
    constraint.Particle1 = particle1;
    constraint.Particle2 = particle2;
    constraint.Particle3 = particle3;
    constraint.RestVolume = ComputeSignedTetrahedronVolume(
        particles[particle0].Position,
        particles[particle1].Position,
        particles[particle2].Position,
        particles[particle3].Position);
    constraint.Compliance = std::max(0.0f, compliance);
    constraint.Lambda = 0.0f;
    return constraint;
}

} // namespace ph
} // namespace Raven
