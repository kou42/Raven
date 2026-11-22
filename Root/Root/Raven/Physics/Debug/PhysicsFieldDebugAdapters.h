#pragma once

#include "Raven/Physics/Debug/PhysicsFieldDebugSampler.h"

namespace Raven::ph
{
class DenseSDFSource;
class SparseSDFSnapshot;

enum class PhysicsSDFDebugStatus { Valid, OutsideGrid, OutsideBand, MissingSupport, Invalid };

struct PhysicsSDFDebugSample
{
    math::Vec3 Position;
    float Distance = 0.0f;
    PhysicsSDFDebugStatus Status = PhysicsSDFDebugStatus::Invalid;
};

PhysicsSDFDebugSample SamplePhysicsDebugSDF(const DenseSDFSource& source, const math::Vec3& position);
PhysicsSDFDebugSample SamplePhysicsDebugSDF(const SparseSDFSnapshot& snapshot, const math::Vec3& position);
}
