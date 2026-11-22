#include "Raven/Physics/Debug/PhysicsFieldDebugAdapters.h"

#include "Raven/Physics/Field/DenseSDF.h"
#include "Raven/Physics/Field/SparseSDF.h"

#include <cmath>

namespace Raven::ph
{
PhysicsSDFDebugSample SamplePhysicsDebugSDF(const DenseSDFSource& source, const math::Vec3& position)
{
    const float distance = source.Evaluate(position);
    return { position, distance, std::isfinite(distance) ? PhysicsSDFDebugStatus::Valid : PhysicsSDFDebugStatus::Invalid };
}

PhysicsSDFDebugSample SamplePhysicsDebugSDF(const SparseSDFSnapshot& snapshot, const math::Vec3& position)
{
    PhysicsSDFDebugSample sample{ position };
    switch (snapshot.Sample(position, sample.Distance))
    {
    case SparseSDFSampleStatus::Valid: sample.Status = PhysicsSDFDebugStatus::Valid; break;
    case SparseSDFSampleStatus::OutsideGrid: sample.Status = PhysicsSDFDebugStatus::OutsideGrid; break;
    case SparseSDFSampleStatus::OutsideBand: sample.Status = PhysicsSDFDebugStatus::OutsideBand; break;
    case SparseSDFSampleStatus::MissingSupport: sample.Status = PhysicsSDFDebugStatus::MissingSupport; break;
    }
    return sample;
}
}
