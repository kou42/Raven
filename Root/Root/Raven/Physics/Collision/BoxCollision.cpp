#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Collision/OBB.h"

namespace Raven::ph
{
namespace
{
constexpr float AxisEpsilonSquared = 1.0e-10f;
constexpr float ClipEpsilon = 1.0e-5f;

enum class SATFeatureType
{
    FaceA,
    FaceB,
    EdgeEdge
};

struct SATResult
{
    bool Intersect = false;
    float Penetration = std::numeric_limits<float>::max();
    math::Vec3 Normal{ 1.0f, 0.0f, 0.0f }; // A -> B
    SATFeatureType Feature = SATFeatureType::FaceA;
    int AxisA = -1;
    int AxisB = -1;
};

void SetCombinedMaterial(
    const ColliderComponent& colliderA,
    const ColliderComponent& colliderB,
    ContactManifold& manifold)
{
    manifold.Restitution = std::min(
        std::max(colliderA.Restitution, 0.0f),
        std::max(colliderB.Restitution, 0.0f));
    manifold.StaticFriction = std::sqrt(
        std::max(colliderA.StaticFriction, 0.0f)
        * std::max(colliderB.StaticFriction, 0.0f));
    manifold.DynamicFriction = std::sqrt(
        std::max(colliderA.DynamicFriction, 0.0f)
        * std::max(colliderB.DynamicFriction, 0.0f));
    manifold.IsTrigger = colliderA.IsTrigger || colliderB.IsTrigger;
}

float ProjectionRadius(const OBB& box, const math::Vec3& axis)
{
    return std::abs(math::Vec3::Dot(axis, box.Axis[0])) * box.HalfExtents.x
        + std::abs(math::Vec3::Dot(axis, box.Axis[1])) * box.HalfExtents.y
        + std::abs(math::Vec3::Dot(axis, box.Axis[2])) * box.HalfExtents.z;
}

bool TestAxis(
    const OBB& a,
    const OBB& b,
    const math::Vec3& centerDelta,
    const math::Vec3& rawAxis,
    SATFeatureType feature,
    int axisA,
    int axisB,
    SATResult& result)
{
    const float lengthSquared = rawAxis.LengthSq();
    if (lengthSquared <= AxisEpsilonSquared)
    {
        // 2辺がほぼ平行なcross axisは方向が数値的に不安定で、独立した分離軸にも
        // ならないためSAT候補から除外します。
        return true;
    }

    math::Vec3 axis = rawAxis / std::sqrt(lengthSquared);
    const float distance = std::abs(math::Vec3::Dot(centerDelta, axis));
    const float radius = ProjectionRadius(a, axis) + ProjectionRadius(b, axis);
    const float overlap = radius - distance;
    if (overlap < 0.0f)
    {
        return false;
    }

    if (overlap < result.Penetration)
    {
        // ContactManifoldの規約は常にA -> Bです。
        if (math::Vec3::Dot(centerDelta, axis) < 0.0f)
        {
            axis = -axis;
        }
        result.Penetration = overlap;
        result.Normal = axis;
        result.Feature = feature;
        result.AxisA = axisA;
        result.AxisB = axisB;
    }
    return true;
}

bool ComputeSAT(const OBB& a, const OBB& b, SATResult& result)
{
    result = SATResult{};
    const math::Vec3 centerDelta = b.Center - a.Center;

    // ========================================================================
    // OBB-OBB 15-axis SAT
    // ========================================================================
    //  3: Aのface normal
    //  3: Bのface normal
    //  9: Ai x Bj (edge-edge候補)
    // の合計15軸を調べます。どれか1軸で投影区間が分離すれば即非衝突です。
    for (int i = 0; i < 3; ++i)
    {
        if (!TestAxis(a, b, centerDelta, a.Axis[i], SATFeatureType::FaceA, i, -1, result)) return false;
    }
    for (int j = 0; j < 3; ++j)
    {
        if (!TestAxis(a, b, centerDelta, b.Axis[j], SATFeatureType::FaceB, -1, j, result)) return false;
    }
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            if (!TestAxis(a, b, centerDelta, math::Vec3::Cross(a.Axis[i], b.Axis[j]),
                    SATFeatureType::EdgeEdge, i, j, result)) return false;
        }
    }

    result.Intersect = true;
    return true;
}

std::array<math::Vec3, 4> GetFaceVertices(const OBB& box, int faceAxis, float faceSign)
{
    int tangent0 = (faceAxis + 1) % 3;
    int tangent1 = (faceAxis + 2) % 3;
    const math::Vec3 faceCenter = box.Center + box.Axis[faceAxis] * (box.HalfExtents[faceAxis] * faceSign);
    const math::Vec3 u = box.Axis[tangent0] * box.HalfExtents[tangent0];
    const math::Vec3 v = box.Axis[tangent1] * box.HalfExtents[tangent1];
    return { faceCenter - u - v, faceCenter + u - v, faceCenter + u + v, faceCenter - u + v };
}

std::vector<math::Vec3> ClipPolygonAgainstPlane(
    const std::vector<math::Vec3>& input,
    const math::Vec3& normal,
    float planeOffset)
{
    std::vector<math::Vec3> output;
    if (input.empty()) return output;
    output.reserve(input.size() + 1);

    math::Vec3 previous = input.back();
    float previousDistance = math::Vec3::Dot(normal, previous) - planeOffset;
    bool previousInside = previousDistance <= ClipEpsilon;

    for (const math::Vec3& current : input)
    {
        const float currentDistance = math::Vec3::Dot(normal, current) - planeOffset;
        const bool currentInside = currentDistance <= ClipEpsilon;

        if (currentInside != previousInside)
        {
            const float denominator = previousDistance - currentDistance;
            if (std::abs(denominator) > 1.0e-8f)
            {
                const float t = previousDistance / denominator;
                output.push_back(previous + (current - previous) * t);
            }
        }
        if (currentInside)
        {
            output.push_back(current);
        }

        previous = current;
        previousDistance = currentDistance;
        previousInside = currentInside;
    }
    return output;
}

int FindIncidentFace(const OBB& incident, const math::Vec3& referenceNormal, float& outSign)
{
    int bestAxis = 0;
    float bestDot = std::abs(math::Vec3::Dot(referenceNormal, incident.Axis[0]));
    for (int axis = 1; axis < 3; ++axis)
    {
        const float candidate = std::abs(math::Vec3::Dot(referenceNormal, incident.Axis[axis]));
        if (candidate > bestDot)
        {
            bestDot = candidate;
            bestAxis = axis;
        }
    }

    // Incident faceはReference faceへ最も向き合う面、つまり法線がreferenceNormalと
    // 反対を向く側を選びます。
    outSign = math::Vec3::Dot(referenceNormal, incident.Axis[bestAxis]) > 0.0f ? -1.0f : 1.0f;
    return bestAxis;
}

void GenerateFaceContacts(
    const OBB& reference,
    const OBB& incident,
    int referenceAxis,
    const math::Vec3& referenceNormal,
    ContactManifold& manifold)
{
    // referenceNormalはReference -> Incidentを向くよう揃えます。
    const float referenceSign = math::Vec3::Dot(referenceNormal, reference.Axis[referenceAxis]) >= 0.0f ? 1.0f : -1.0f;
    const math::Vec3 referenceFaceCenter = reference.Center
        + reference.Axis[referenceAxis] * (reference.HalfExtents[referenceAxis] * referenceSign);

    float incidentSign = 1.0f;
    const int incidentAxis = FindIncidentFace(incident, referenceNormal, incidentSign);
    const auto incidentVertices = GetFaceVertices(incident, incidentAxis, incidentSign);
    std::vector<math::Vec3> polygon(incidentVertices.begin(), incidentVertices.end());

    // Reference faceの4側面でIncident polygonを順番にclipします。
    // Sutherland-Hodgmanを3D平面へそのまま適用している形です。
    const int tangent0 = (referenceAxis + 1) % 3;
    const int tangent1 = (referenceAxis + 2) % 3;
    const math::Vec3 u = reference.Axis[tangent0];
    const math::Vec3 v = reference.Axis[tangent1];
    const float hu = reference.HalfExtents[tangent0];
    const float hv = reference.HalfExtents[tangent1];

    polygon = ClipPolygonAgainstPlane(polygon, u, math::Vec3::Dot(u, referenceFaceCenter) + hu);
    polygon = ClipPolygonAgainstPlane(polygon, -u, math::Vec3::Dot(-u, referenceFaceCenter) + hu);
    polygon = ClipPolygonAgainstPlane(polygon, v, math::Vec3::Dot(v, referenceFaceCenter) + hv);
    polygon = ClipPolygonAgainstPlane(polygon, -v, math::Vec3::Dot(-v, referenceFaceCenter) + hv);

    for (const math::Vec3& vertex : polygon)
    {
        if (manifold.PointCount >= ContactManifold::MaxContactPointCount) break;

        // Reference face planeよりIncident側へ出ている点は接触点ではありません。
        // 負のsignedDistanceがReference内部への貫通量です。
        const float signedDistance = math::Vec3::Dot(referenceNormal, vertex - referenceFaceCenter);
        if (signedDistance <= ClipEpsilon)
        {
            ContactPoint point{};
            point.Penetration = std::max(-signedDistance, 0.0f);

            // Incident頂点そのものではなく、Reference面との中間へ置くことで
            // 深い貫通時にもContact markerが片側へ偏りすぎないようにします。
            point.Position = vertex - referenceNormal * (signedDistance * 0.5f);
            manifold.AddPoint(point);
        }
    }
}

void ClosestPointsOnSegments(
    const math::Vec3& p1, const math::Vec3& q1,
    const math::Vec3& p2, const math::Vec3& q2,
    math::Vec3& outA, math::Vec3& outB)
{
    const math::Vec3 d1 = q1 - p1;
    const math::Vec3 d2 = q2 - p2;
    const math::Vec3 r = p1 - p2;
    const float a = math::Vec3::Dot(d1, d1);
    const float e = math::Vec3::Dot(d2, d2);
    const float f = math::Vec3::Dot(d2, r);

    float s = 0.0f;
    float t = 0.0f;
    if (a <= 1.0e-12f && e <= 1.0e-12f)
    {
        outA = p1; outB = p2; return;
    }
    if (a <= 1.0e-12f)
    {
        t = std::clamp(f / e, 0.0f, 1.0f);
    }
    else
    {
        const float c = math::Vec3::Dot(d1, r);
        if (e <= 1.0e-12f)
        {
            s = std::clamp(-c / a, 0.0f, 1.0f);
        }
        else
        {
            const float b = math::Vec3::Dot(d1, d2);
            const float denominator = a * e - b * b;
            if (std::abs(denominator) > 1.0e-12f)
            {
                s = std::clamp((b * f - c * e) / denominator, 0.0f, 1.0f);
            }
            t = (b * s + f) / e;
            if (t < 0.0f)
            {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            }
            else if (t > 1.0f)
            {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    outA = p1 + d1 * s;
    outB = p2 + d2 * t;
}

void GetSupportEdge(
    const OBB& box,
    int edgeAxis,
    const math::Vec3& supportDirection,
    math::Vec3& outStart,
    math::Vec3& outEnd)
{
    math::Vec3 edgeCenter = box.Center;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (axis == edgeAxis) continue;
        const float sign = math::Vec3::Dot(supportDirection, box.Axis[axis]) >= 0.0f ? 1.0f : -1.0f;
        edgeCenter += box.Axis[axis] * (box.HalfExtents[axis] * sign);
    }
    const math::Vec3 edgeVector = box.Axis[edgeAxis] * box.HalfExtents[edgeAxis];
    outStart = edgeCenter - edgeVector;
    outEnd = edgeCenter + edgeVector;
}

void GenerateEdgeContact(
    const OBB& a,
    const OBB& b,
    const SATResult& sat,
    ContactManifold& manifold)
{
    math::Vec3 a0{}, a1{}, b0{}, b1{};
    GetSupportEdge(a, sat.AxisA, sat.Normal, a0, a1);
    GetSupportEdge(b, sat.AxisB, -sat.Normal, b0, b1);

    math::Vec3 pointA{}, pointB{};
    ClosestPointsOnSegments(a0, a1, b0, b1, pointA, pointB);

    ContactPoint point{};
    point.Position = (pointA + pointB) * 0.5f;
    point.Penetration = sat.Penetration;
    manifold.AddPoint(point);
}
}

bool GenerateBoxBoxManifold(
    Entity boxEntityA,
    const TransformComponent& boxTransformA,
    const ColliderComponent& boxColliderA,
    Entity boxEntityB,
    const TransformComponent& boxTransformB,
    const ColliderComponent& boxColliderB,
    ContactManifold& outManifold)
{
    OBB a{}, b{};
    if (!ComputeBoxOBB(boxTransformA, boxColliderA, a)
        || !ComputeBoxOBB(boxTransformB, boxColliderB, b))
    {
        return false;
    }

    SATResult sat{};
    if (!ComputeSAT(a, b, sat))
    {
        return false;
    }

    outManifold = ContactManifold{};
    outManifold.A = boxEntityA;
    outManifold.B = boxEntityB;
    outManifold.Normal = sat.Normal;
    SetCombinedMaterial(boxColliderA, boxColliderB, outManifold);

    if (sat.Feature == SATFeatureType::FaceA)
    {
        GenerateFaceContacts(a, b, sat.AxisA, sat.Normal, outManifold);
    }
    else if (sat.Feature == SATFeatureType::FaceB)
    {
        // ReferenceがBの場合、Reference -> IncidentはB -> AなのでSAT normalの逆です。
        // Manifold.Normal自体は規約どおりA -> Bのまま保持します。
        GenerateFaceContacts(b, a, sat.AxisB, -sat.Normal, outManifold);
    }
    else
    {
        GenerateEdgeContact(a, b, sat, outManifold);
    }

    // 数値的に極端なface clippingで点が消えた場合の安全網です。
    // SATでは確実に交差しているため、support point中点を1点残してSolverへ渡します。
    if (outManifold.PointCount == 0)
    {
        ContactPoint fallback{};
        fallback.Position = (a.Support(sat.Normal) + b.Support(-sat.Normal)) * 0.5f;
        fallback.Penetration = sat.Penetration;
        outManifold.AddPoint(fallback);
    }

    return true;
}

} // namespace Raven::ph
