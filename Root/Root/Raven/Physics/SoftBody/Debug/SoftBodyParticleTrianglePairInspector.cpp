#include "Raven/Physics/SoftBody/Debug/SoftBodyParticleTrianglePairInspector.h"

#include <algorithm>
#include <cmath>

namespace Raven
{
namespace ph
{
namespace
{
float ComputeOutsideDistance(float value, float minimum, float maximum)
{
    if (value < minimum)
    {
        return minimum - value;
    }
    if (value > maximum)
    {
        return value - maximum;
    }
    return 0.0f;
}

bool TryNormalizeDistance(float scaledDistance, float normalLengthSq, float& outDistance)
{
    if (normalLengthSq <= math::Epsilon * math::Epsilon)
    {
        outDistance = 0.0f;
        return false;
    }

    outDistance = scaledDistance / std::sqrt(normalLengthSq);
    return true;
}
} // namespace

bool SoftBodyParticleTrianglePairInspector::Inspect(
    const std::vector<SoftBodyParticle>& particles,
    const std::vector<uint32_t>& triangleIndices,
    const SoftBodyParticleTriangleCandidateDebugSnapshot& snapshot,
    uint32_t particleIndex,
    uint32_t triangleIndex,
    SoftBodyParticleTrianglePairInspection& outInspection)
{
    outInspection = {};

    if (particleIndex >= particles.size())
    {
        return false;
    }

    const std::size_t triangleBaseIndex = static_cast<std::size_t>(triangleIndex) * 3u;
    if (triangleBaseIndex + 2u >= triangleIndices.size())
    {
        return false;
    }

    // Snapshot内に実際に存在するCell Candidateかを先に確認します。
    // 任意のParticle/Triangleを計算できるAPIにすると、Broad Phaseで一度も出会っていないPairまで
    // Reject結果として誤解されるため、InspectorはSnapshot Recordを正とします。
    const SoftBodyParticleTriangleCandidateDebugInfo* selectedRecord = nullptr;
    for (const SoftBodyParticleTriangleCandidateDebugInfo& record : snapshot.Records)
    {
        if (record.ParticleIndex == particleIndex
            && record.TriangleIndex == triangleIndex)
        {
            selectedRecord = &record;
            break;
        }
    }

    if (selectedRecord == nullptr)
    {
        return false;
    }

    const uint32_t particleA = triangleIndices[triangleBaseIndex];
    const uint32_t particleB = triangleIndices[triangleBaseIndex + 1u];
    const uint32_t particleC = triangleIndices[triangleBaseIndex + 2u];
    if (particleA >= particles.size()
        || particleB >= particles.size()
        || particleC >= particles.size())
    {
        return false;
    }

    const math::Vec3& point = particles[particleIndex].Position;
    const math::Vec3& a = particles[particleA].Position;
    const math::Vec3& b = particles[particleB].Position;
    const math::Vec3& c = particles[particleC].Position;
    const float thickness = std::max(snapshot.Thickness, 0.0f);

    outInspection.ParticleIndex = particleIndex;
    outInspection.TriangleIndex = triangleIndex;
    outInspection.Reason = selectedRecord->Reason;
    outInspection.Thickness = thickness;

    // ========================================================================
    // Expanded AABB
    // ========================================================================
    const float minimumX = std::min({ a.x, b.x, c.x }) - thickness;
    const float minimumY = std::min({ a.y, b.y, c.y }) - thickness;
    const float minimumZ = std::min({ a.z, b.z, c.z }) - thickness;
    const float maximumX = std::max({ a.x, b.x, c.x }) + thickness;
    const float maximumY = std::max({ a.y, b.y, c.y }) + thickness;
    const float maximumZ = std::max({ a.z, b.z, c.z }) + thickness;

    outInspection.AABBOutsideDistance.x = ComputeOutsideDistance(point.x, minimumX, maximumX);
    outInspection.AABBOutsideDistance.y = ComputeOutsideDistance(point.y, minimumY, maximumY);
    outInspection.AABBOutsideDistance.z = ComputeOutsideDistance(point.z, minimumZ, maximumZ);

    // ========================================================================
    // Plane Distance
    // ========================================================================
    // Runtime Candidate生成と同じ非正規化法線からscaled distanceを求め、表示時だけWorld Space距離へ戻します。
    const math::Vec3 edgeABVector = b - a;
    const math::Vec3 edgeACVector = c - a;
    const math::Vec3 planeNormal = math::Vec3::Cross(edgeABVector, edgeACVector);
    const float planeNormalLengthSq = planeNormal.LengthSq();
    const float planeScaledDistance =
        math::Vec3::Dot(planeNormal, point) - math::Vec3::Dot(planeNormal, a);

    outInspection.PlaneValid = TryNormalizeDistance(
        planeScaledDistance,
        planeNormalLengthSq,
        outInspection.PlaneSignedDistance);

    // ========================================================================
    // Edge Half-Space Distances
    // ========================================================================
    if (outInspection.PlaneValid == true)
    {
        const math::Vec3 edgeBCVector = c - b;
        const math::Vec3 edgeCAVector = a - c;
        const math::Vec3 edgeABNormal = math::Vec3::Cross(planeNormal, edgeABVector);
        const math::Vec3 edgeBCNormal = math::Vec3::Cross(planeNormal, edgeBCVector);
        const math::Vec3 edgeCANormal = math::Vec3::Cross(planeNormal, edgeCAVector);

        const float edgeABScaledDistance =
            math::Vec3::Dot(edgeABNormal, point) - math::Vec3::Dot(edgeABNormal, a);
        const float edgeBCScaledDistance =
            math::Vec3::Dot(edgeBCNormal, point) - math::Vec3::Dot(edgeBCNormal, b);
        const float edgeCAScaledDistance =
            math::Vec3::Dot(edgeCANormal, point) - math::Vec3::Dot(edgeCANormal, c);

        const bool edgeABValid = TryNormalizeDistance(
            edgeABScaledDistance,
            edgeABNormal.LengthSq(),
            outInspection.EdgeABSignedDistance);
        const bool edgeBCValid = TryNormalizeDistance(
            edgeBCScaledDistance,
            edgeBCNormal.LengthSq(),
            outInspection.EdgeBCSignedDistance);
        const bool edgeCAValid = TryNormalizeDistance(
            edgeCAScaledDistance,
            edgeCANormal.LengthSq(),
            outInspection.EdgeCASignedDistance);

        outInspection.EdgeHalfSpaceValid =
            edgeABValid == true
            && edgeBCValid == true
            && edgeCAValid == true;
    }

    outInspection.Valid = true;
    return true;
}

} // namespace ph
} // namespace Raven
