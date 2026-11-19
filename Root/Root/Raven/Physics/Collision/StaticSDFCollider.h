#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"

namespace Raven::ph
{
class DenseSDFSource;
class SparseSDFSnapshot;

// CPUへ明示的に渡されたworld-spaceの符号付きField。GPUの同期やReadbackは呼びません。
class StaticSDFBoundary
{
public:
    static Ref<const StaticSDFBoundary> FromAnalytic(Ref<const DenseSDFSource> source, float normalStep);
    static Ref<const StaticSDFBoundary> FromSnapshot(Ref<const SparseSDFSnapshot> snapshot, float normalStep);
    bool TryDistance(math::Vec3 position, float& distance) const;
    bool TryNormal(math::Vec3 position, math::Vec3& normal) const;

private:
    StaticSDFBoundary(Ref<const DenseSDFSource> source, Ref<const SparseSDFSnapshot> snapshot, float normalStep);
    Ref<const DenseSDFSource> m_Source;
    Ref<const SparseSDFSnapshot> m_Snapshot;
    float m_NormalStep;
};

struct SDFSphereContact
{
    math::Vec3 SurfacePosition{};
    math::Vec3 OutwardNormal{};
    float Penetration = 0.0f;
};

bool GenerateSDFSphereContact(const StaticSDFBoundary& boundary, math::Vec3 center,
    float radius, SDFSphereContact& contact);
} // namespace Raven::ph

namespace Raven
{
// TransformはSolverの静的アンカーに必要です。Fieldの位置は入力のworld-space座標で固定します。
struct StaticSDFColliderComponent
{
    Ref<const ph::StaticSDFBoundary> Boundary;
    float Restitution = 0.0f;
    float StaticFriction = 0.6f;
    float DynamicFriction = 0.4f;
};
} // namespace Raven
