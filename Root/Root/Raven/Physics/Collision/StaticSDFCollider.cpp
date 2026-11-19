#include "Raven/Physics/Collision/StaticSDFCollider.h"
#include "Raven/Physics/Field/SparseSDF.h"

#include <cmath>

namespace Raven::ph
{
StaticSDFBoundary::StaticSDFBoundary(Ref<const DenseSDFSource> source,
    Ref<const SparseSDFSnapshot> snapshot, float normalStep)
    : m_Source(std::move(source)), m_Snapshot(std::move(snapshot)), m_NormalStep(normalStep)
{
}

Ref<const StaticSDFBoundary> StaticSDFBoundary::FromAnalytic(Ref<const DenseSDFSource> source, float normalStep)
{
    if (source == nullptr || source->GetMode() == DenseSDFSource::Mode::UnsignedMesh ||
        std::isfinite(normalStep) == false || normalStep <= 0.0f)
    {
        return nullptr;
    }
    return Ref<const StaticSDFBoundary>(new StaticSDFBoundary(std::move(source), nullptr, normalStep));
}

Ref<const StaticSDFBoundary> StaticSDFBoundary::FromSnapshot(Ref<const SparseSDFSnapshot> snapshot, float normalStep)
{
    if (snapshot == nullptr || snapshot->IsSigned() == false || std::isfinite(normalStep) == false || normalStep <= 0.0f)
    {
        return nullptr;
    }
    return Ref<const StaticSDFBoundary>(new StaticSDFBoundary(nullptr, std::move(snapshot), normalStep));
}

bool StaticSDFBoundary::TryDistance(math::Vec3 position, float& distance) const
{
    if (m_Snapshot != nullptr)
    {
        return m_Snapshot->TrySample(position, distance);
    }
    const float sample = m_Source->Evaluate(position);
    if (std::isfinite(sample) == false)
    {
        return false;
    }
    distance = sample;
    return true;
}

bool StaticSDFBoundary::TryNormal(math::Vec3 position, math::Vec3& normal) const
{
    math::Vec3 gradient{};
    if (m_Source != nullptr && m_Source->GetMode() == DenseSDFSource::Mode::Sphere)
    {
        gradient = position - m_Source->GetCenter();
    }
    else
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            math::Vec3 step{};
            step[axis] = m_NormalStep;
            float lo = 0.0f;
            float hi = 0.0f;
            if (TryDistance(position - step, lo) == false || TryDistance(position + step, hi) == false)
            {
                return false;
            }
            gradient[axis] = (hi - lo) / (2.0f * m_NormalStep);
        }
    }
    const float length = gradient.Length();
    if (std::isfinite(length) == false || length <= math::Epsilon)
    {
        return false;
    }
    normal = gradient / length;
    return true;
}

bool GenerateSDFSphereContact(const StaticSDFBoundary& boundary, math::Vec3 center,
    float radius, SDFSphereContact& contact)
{
    float distance = 0.0f;
    math::Vec3 normal{};
    if (std::isfinite(radius) == false || radius < 0.0f ||
        boundary.TryDistance(center, distance) == false || distance > radius ||
        boundary.TryNormal(center, normal) == false)
    {
        return false;
    }
    contact = {center - normal * distance, normal, radius - distance};
    return true;
}
} // namespace Raven::ph
