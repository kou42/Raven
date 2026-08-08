#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

// Box-Box Narrow Phaseの方針:
// 1) SATで最小貫通軸と接触タイプを特定
// 2) Face接触はIncident面をクリップして複数点生成
// 3) Edge接触は最近接線分点へ縮約
// 4) 退化時はSupport中点で最低1点を保証

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
    math::Vec3 Normal{ 1.0f, 0.0f, 0.0f };
    SATFeatureType Feature = SATFeatureType::FaceA;
    int AxisA = -1;
    int AxisB = -1;
};

// Face/Vertex/EdgeのFeature IDは、接触点の永続対応付けに利用します。
uint8_t FaceID(int axis, float sign)
{
    return static_cast<uint8_t>(axis * 2 + (sign > 0.0f ? 1 : 0));
}

uint8_t VertexID(const OBB& box, const math::Vec3& point)
{
    // 各軸の符号を3bitへ詰め、頂点インデックスを一意に表現します。
    const math::Vec3 delta = point - box.Center;
    uint8_t id = 0;

    for (int axis = 0; axis < 3; ++axis)
    {
        if (math::Vec3::Dot(delta, box.Axis[axis]) >= 0.0f)
        {
            id |= static_cast<uint8_t>(1u << axis);
        }
    }

    return id;
}

uint8_t EdgeID(const OBB& box, int axis, const math::Vec3& support)
{
    // 主軸(axis) + 残り2軸の符号で12本の辺を識別します。
    const math::Vec3 delta = support - box.Center;
    uint8_t sides = 0;
    uint8_t bit = 0;

    for (int i = 0; i < 3; ++i)
    {
        if (i == axis)
        {
            continue;
        }

        if (math::Vec3::Dot(delta, box.Axis[i]) >= 0.0f)
        {
            sides |= static_cast<uint8_t>(1u << bit);
        }

        ++bit;
    }

    return static_cast<uint8_t>(axis * 4 + sides);
}

// 反発係数は最小値、摩擦係数は幾何平均で合成し、過大な反応を避けます。
void SetCombinedMaterial(
    const ColliderComponent& a,
    const ColliderComponent& b,
    ContactManifold& manifold)
{
    manifold.Restitution = std::min(
        std::max(a.Restitution, 0.0f),
        std::max(b.Restitution, 0.0f));
    manifold.StaticFriction = std::sqrt(
        std::max(a.StaticFriction, 0.0f)
        * std::max(b.StaticFriction, 0.0f));
    manifold.DynamicFriction = std::sqrt(
        std::max(a.DynamicFriction, 0.0f)
        * std::max(b.DynamicFriction, 0.0f));
    manifold.IsTrigger = a.IsTrigger || b.IsTrigger;
}

float ProjectionRadius(const OBB& box, const math::Vec3& axis)
{
    return std::abs(math::Vec3::Dot(axis, box.Axis[0])) * box.HalfExtents.x
        + std::abs(math::Vec3::Dot(axis, box.Axis[1])) * box.HalfExtents.y
        + std::abs(math::Vec3::Dot(axis, box.Axis[2])) * box.HalfExtents.z;
}

// SATの各軸で投影半径を比較し、貫通深度が最小の軸を接触法線候補として残します。
bool TestAxis(
    const OBB& a,
    const OBB& b,
    const math::Vec3& delta,
    const math::Vec3& rawAxis,
    SATFeatureType feature,
    int axisA,
    int axisB,
    SATResult& result)
{
    const float axisLengthSq = rawAxis.LengthSq();
    if (axisLengthSq <= AxisEpsilonSquared)
    {
        return true;
    }

    math::Vec3 axis = rawAxis / std::sqrt(axisLengthSq);
    const float overlap = ProjectionRadius(a, axis)
        + ProjectionRadius(b, axis)
        - std::abs(math::Vec3::Dot(delta, axis));

    if (overlap < 0.0f)
    {
        return false;
    }

    if (overlap < result.Penetration)
    {
        if (math::Vec3::Dot(delta, axis) < 0.0f)
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
    const math::Vec3 delta = b.Center - a.Center;

    // Face法線6本 + 交差軸9本を評価し、最小貫通軸を接触法線として採用します。
    // 1本でも分離軸が見つかれば非交差と確定できます。
    for (int i = 0; i < 3; ++i)
    {
        if (TestAxis(a, b, delta, a.Axis[i], SATFeatureType::FaceA, i, -1, result) == false)
        {
            return false;
        }
    }

    for (int j = 0; j < 3; ++j)
    {
        if (TestAxis(a, b, delta, b.Axis[j], SATFeatureType::FaceB, -1, j, result) == false)
        {
            return false;
        }
    }

    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            const math::Vec3 axis = math::Vec3::Cross(a.Axis[i], b.Axis[j]);
            if (TestAxis(a, b, delta, axis, SATFeatureType::EdgeEdge, i, j, result) == false)
            {
                return false;
            }
        }
    }

    result.Intersect = true;
    return true;
}

std::array<math::Vec3, 4> GetFaceVertices(const OBB& box, int axis, float sign)
{
    const int tangent0 = (axis + 1) % 3;
    const int tangent1 = (axis + 2) % 3;
    const math::Vec3 center = box.Center + box.Axis[axis] * (box.HalfExtents[axis] * sign);
    const math::Vec3 u = box.Axis[tangent0] * box.HalfExtents[tangent0];
    const math::Vec3 v = box.Axis[tangent1] * box.HalfExtents[tangent1];

    return { center - u - v, center + u - v, center + u + v, center - u + v };
}

// Incident面の多角形をReference面の側面4平面でクリップし、接触候補点を抽出します。
std::vector<math::Vec3> ClipPolygonAgainstPlane(
    const std::vector<math::Vec3>& input,
    const math::Vec3& normal,
    float offset)
{
    std::vector<math::Vec3> output;
    if (input.empty())
    {
        return output;
    }

    math::Vec3 previous = input.back();
    float previousDistance = math::Vec3::Dot(normal, previous) - offset;
    bool previousInside = previousDistance <= ClipEpsilon;

    for (const auto& current : input)
    {
        const float currentDistance = math::Vec3::Dot(normal, current) - offset;
        const bool currentInside = currentDistance <= ClipEpsilon;

        if (currentInside != previousInside)
        {
            const float denominator = previousDistance - currentDistance;
            if (std::abs(denominator) > 1.0e-8f)
            {
                output.push_back(previous + (current - previous) * (previousDistance / denominator));
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

// Incident Boxの面法線候補から、Reference法線に最も逆向きの面を選択します。
int FindIncidentFace(const OBB& incident, const math::Vec3& normal, float& outSign)
{
    int bestAxis = 0;
    float bestDot = std::abs(math::Vec3::Dot(normal, incident.Axis[0]));

    for (int axis = 1; axis < 3; ++axis)
    {
        const float dot = std::abs(math::Vec3::Dot(normal, incident.Axis[axis]));
        if (dot > bestDot)
        {
            bestDot = dot;
            bestAxis = axis;
        }
    }

    outSign = math::Vec3::Dot(normal, incident.Axis[bestAxis]) > 0.0f ? -1.0f : 1.0f;
    return bestAxis;
}

// Face接触ではFeature IDを確定し、Warm Startで再利用できる接触点を構築します。
void GenerateFaceContacts(
    const OBB& reference,
    const OBB& incident,
    int referenceAxis,
    const math::Vec3& referenceNormal,
    bool referenceIsA,
    ContactManifold& manifold)
{
    // Reference面を固定し、Incident面ポリゴンを周囲4平面で切り取ります。
    const float referenceSign = math::Vec3::Dot(referenceNormal, reference.Axis[referenceAxis]) >= 0.0f ? 1.0f : -1.0f;
    const math::Vec3 faceCenter = reference.Center
        + reference.Axis[referenceAxis] * (reference.HalfExtents[referenceAxis] * referenceSign);

    float incidentSign = 1.0f;
    const int incidentAxis = FindIncidentFace(incident, referenceNormal, incidentSign);
    const auto vertices = GetFaceVertices(incident, incidentAxis, incidentSign);

    std::vector<math::Vec3> polygon(vertices.begin(), vertices.end());

    const int tangent0 = (referenceAxis + 1) % 3;
    const int tangent1 = (referenceAxis + 2) % 3;
    const math::Vec3 u = reference.Axis[tangent0];
    const math::Vec3 v = reference.Axis[tangent1];
    const float halfU = reference.HalfExtents[tangent0];
    const float halfV = reference.HalfExtents[tangent1];

    polygon = ClipPolygonAgainstPlane(polygon, u, math::Vec3::Dot(u, faceCenter) + halfU);
    polygon = ClipPolygonAgainstPlane(polygon, -u, math::Vec3::Dot(-u, faceCenter) + halfU);
    polygon = ClipPolygonAgainstPlane(polygon, v, math::Vec3::Dot(v, faceCenter) + halfV);
    polygon = ClipPolygonAgainstPlane(polygon, -v, math::Vec3::Dot(-v, faceCenter) + halfV);

    // 参照面へ投影した点のみを採用し、接触点ごとにFeature IDを保存します。
    for (const auto& vertex : polygon)
    {
        if (manifold.PointCount >= ContactManifold::MaxContactPointCount)
        {
            break;
        }

        const float signedDistance = math::Vec3::Dot(referenceNormal, vertex - faceCenter);
        if (signedDistance <= ClipEpsilon)
        {
            ContactPoint point{};
            point.Penetration = std::max(-signedDistance, 0.0f);
            point.Position = vertex - referenceNormal * (signedDistance * 0.5f);

            const uint8_t referenceFace = FaceID(referenceAxis, referenceSign);
            const uint8_t incidentVertex = VertexID(incident, vertex);

            if (referenceIsA)
            {
                point.Feature.TypeA = ContactFeatureType::Face;
                point.Feature.IndexA = referenceFace;
                point.Feature.TypeB = ContactFeatureType::Vertex;
                point.Feature.IndexB = incidentVertex;
            }
            else
            {
                point.Feature.TypeA = ContactFeatureType::Vertex;
                point.Feature.IndexA = incidentVertex;
                point.Feature.TypeB = ContactFeatureType::Face;
                point.Feature.IndexB = referenceFace;
            }

            manifold.AddPoint(point);
        }
    }
}

// Edge-Edge接触は最近接2線分点から1つの接触点へ縮約します。
void ClosestPointsOnSegments(
    const math::Vec3& p1,
    const math::Vec3& q1,
    const math::Vec3& p2,
    const math::Vec3& q2,
    math::Vec3& outA,
    math::Vec3& outB)
{
    // 2線分の最近接点をclamp付きで求め、Edge接触の代表点に使います。
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
        outA = p1;
        outB = p2;
        return;
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

// SATで得た法線方向へ最も張り出した辺を選び、Edge-Edge接触候補を作ります。
void GetSupportEdge(
    const OBB& box,
    int axis,
    const math::Vec3& direction,
    math::Vec3& outStart,
    math::Vec3& outEnd)
{
    math::Vec3 center = box.Center;

    for (int i = 0; i < 3; ++i)
    {
        if (i == axis)
        {
            continue;
        }

        const float sign = math::Vec3::Dot(direction, box.Axis[i]) >= 0.0f ? 1.0f : -1.0f;
        center += box.Axis[i] * (box.HalfExtents[i] * sign);
    }

    const math::Vec3 edgeVector = box.Axis[axis] * box.HalfExtents[axis];
    outStart = center - edgeVector;
    outEnd = center + edgeVector;
}

// Edge-Edge接触生成:
// SATで得た両Boxの支持辺から最近接2点を取り、中央を接触点として採用します。
void GenerateEdgeContact(const OBB& a, const OBB& b, const SATResult& sat, ContactManifold& manifold)
{
    math::Vec3 aStart{};
    math::Vec3 aEnd{};
    math::Vec3 bStart{};
    math::Vec3 bEnd{};

    GetSupportEdge(a, sat.AxisA, sat.Normal, aStart, aEnd);
    GetSupportEdge(b, sat.AxisB, -sat.Normal, bStart, bEnd);

    math::Vec3 pointA{};
    math::Vec3 pointB{};
    ClosestPointsOnSegments(aStart, aEnd, bStart, bEnd, pointA, pointB);

    ContactPoint point{};
    point.Position = (pointA + pointB) * 0.5f;
    point.Penetration = sat.Penetration;
    point.Feature.TypeA = ContactFeatureType::Edge;
    point.Feature.IndexA = EdgeID(a, sat.AxisA, (aStart + aEnd) * 0.5f);
    point.Feature.TypeB = ContactFeatureType::Edge;
    point.Feature.IndexB = EdgeID(b, sat.AxisB, (bStart + bEnd) * 0.5f);
    manifold.AddPoint(point);
}

} // namespace

// Narrow Phase入口: SAT結果に応じて Face-Face / Edge-Edge 接触生成を切り替えます。
bool GenerateBoxBoxManifold(
    Entity entityA,
    const TransformComponent& transformA,
    const ColliderComponent& colliderA,
    Entity entityB,
    const TransformComponent& transformB,
    const ColliderComponent& colliderB,
    ContactManifold& outManifold)
{
    OBB boxA{};
    OBB boxB{};
    if (ComputeBoxOBB(transformA, colliderA, boxA) == false
        || ComputeBoxOBB(transformB, colliderB, boxB) == false)
    {
        return false;
    }

    SATResult sat{};
    if (ComputeSAT(boxA, boxB, sat) == false)
    {
        return false;
    }

    outManifold = ContactManifold{};
    outManifold.A = entityA;
    outManifold.B = entityB;
    outManifold.Normal = sat.Normal;
    SetCombinedMaterial(colliderA, colliderB, outManifold);

    if (sat.Feature == SATFeatureType::FaceA)
    {
        GenerateFaceContacts(boxA, boxB, sat.AxisA, sat.Normal, true, outManifold);
    }
    else if (sat.Feature == SATFeatureType::FaceB)
    {
        GenerateFaceContacts(boxB, boxA, sat.AxisB, -sat.Normal, false, outManifold);
    }
    else
    {
        GenerateEdgeContact(boxA, boxB, sat, outManifold);
    }

    // クリッピングで点が得られない退化ケースでは、support点の中点をフォールバックにします。
    if (outManifold.PointCount == 0)
    {
        ContactPoint point{};
        point.Position = (boxA.Support(sat.Normal) + boxB.Support(-sat.Normal)) * 0.5f;
        point.Penetration = sat.Penetration;
        outManifold.AddPoint(point);
    }

    return true;
}

} // namespace Raven::ph
