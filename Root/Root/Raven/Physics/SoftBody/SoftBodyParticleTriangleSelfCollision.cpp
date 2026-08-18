#include "Raven/Physics/SoftBody/SoftBodyParticleTriangleSelfCollision.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/SoftBody/SoftBodyCloth.h"
#include "Raven/Physics/SoftBody/SoftBodySolver.h"
#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"

namespace Raven
{
namespace ph
{
namespace
{

struct ClosestPointResult
{
    math::Vec3 Point{};
    float WeightA = 0.0f;
    float WeightB = 0.0f;
    float WeightC = 0.0f;
};

// ============================================================================
// Closest Point on Triangle
// ============================================================================
// Ericson形式の領域判定でTriangleの頂点・辺・面の全領域を扱います。
// Barycentric Weightも同時に返し、Triangle側のPosition補正を3頂点へ分配するために使用します。
ClosestPointResult ComputeClosestPointOnTriangle(
    const math::Vec3& point,
    const math::Vec3& a,
    const math::Vec3& b,
    const math::Vec3& c)
{
    const math::Vec3 ab = b - a;
    const math::Vec3 ac = c - a;
    const math::Vec3 ap = point - a;

    const float d1 = math::Vec3::Dot(ab, ap);
    const float d2 = math::Vec3::Dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f)
    {
        return { a, 1.0f, 0.0f, 0.0f };
    }

    const math::Vec3 bp = point - b;
    const float d3 = math::Vec3::Dot(ab, bp);
    const float d4 = math::Vec3::Dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3)
    {
        return { b, 0.0f, 1.0f, 0.0f };
    }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        const float v = d1 / (d1 - d3);
        return { a + ab * v, 1.0f - v, v, 0.0f };
    }

    const math::Vec3 cp = point - c;
    const float d5 = math::Vec3::Dot(ab, cp);
    const float d6 = math::Vec3::Dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6)
    {
        return { c, 0.0f, 0.0f, 1.0f };
    }

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        const float w = d2 / (d2 - d6);
        return { a + ac * w, 1.0f - w, 0.0f, w };
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        const math::Vec3 bc = c - b;
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return { b + bc * w, 0.0f, 1.0f - w, w };
    }

    const float denominator = va + vb + vc;
    if (std::abs(denominator) <= math::Epsilon)
    {
        // 退化Triangleでは面領域のBarycentric座標を安全に計算できません。
        // 頂点Aをfallbackにし、後段で距離Constraintとして処理します。
        return { a, 1.0f, 0.0f, 0.0f };
    }

    const float inverseDenominator = 1.0f / denominator;
    const float v = vb * inverseDenominator;
    const float w = vc * inverseDenominator;
    const float u = 1.0f - v - w;
    return { a * u + b * v + c * w, u, v, w };
}

void BuildClothTriangles(const SoftBodyCloth& cloth, std::vector<SoftBodyTriangle>& outTriangles)
{
    outTriangles.clear();
    outTriangles.reserve(
        static_cast<std::size_t>(cloth.Rows)
        * static_cast<std::size_t>(cloth.Columns)
        * 2u);

    for (uint32_t row = 0u; row < cloth.Rows; ++row)
    {
        for (uint32_t column = 0u; column < cloth.Columns; ++column)
        {
            const uint32_t topLeft = cloth.GetParticleIndex(row, column);
            const uint32_t topRight = cloth.GetParticleIndex(row, column + 1u);
            const uint32_t bottomLeft = cloth.GetParticleIndex(row + 1u, column);
            const uint32_t bottomRight = cloth.GetParticleIndex(row + 1u, column + 1u);

            // RendererのDynamic Gridと同じ固定Topologyです。
            // T0 = (TL, BL, TR), T1 = (BL, BR, TR)
            outTriangles.push_back({ topLeft, bottomLeft, topRight });
            outTriangles.push_back({ bottomLeft, bottomRight, topRight });
        }
    }
}

bool TriangleContainsParticle(const SoftBodyTriangle& triangle, uint32_t particleIndex)
{
    return triangle.ParticleA == particleIndex
        || triangle.ParticleB == particleIndex
        || triangle.ParticleC == particleIndex;
}

uint64_t MakeParticleTriangleKey(uint32_t particleIndex, uint32_t triangleIndex)
{
    return (static_cast<uint64_t>(particleIndex) << 32u)
        | static_cast<uint64_t>(triangleIndex);
}

} // namespace

void SolveSoftBodyParticleTriangleSelfCollisions(
    SoftBodySolver& solver,
    const SoftBodyCloth& cloth,
    float deltaTime,
    const SoftBodyParticleTriangleSelfCollisionSettings& settings)
{
    if (settings.Enabled == false || deltaTime <= 0.0f)
    {
        return;
    }

    const float thickness = std::max(0.0f, settings.Thickness);
    if (thickness <= math::Epsilon)
    {
        return;
    }

    std::vector<SoftBodyParticle>& particles = solver.GetParticles();
    if (particles.empty() || cloth.Rows == 0u || cloth.Columns == 0u)
    {
        return;
    }

    std::vector<SoftBodyTriangle> triangles;
    BuildClothTriangles(cloth, triangles);
    if (triangles.empty())
    {
        return;
    }

    // CellSizeはThicknessより大きくしておけば、膨張AABBが必要以上に多数セルへ分割されるのを抑えつつ
    // 接触候補を取りこぼしません。Clothの局所三角形サイズより極端に小さくしないことが性能上重要です。
    SoftBodyTriangleSpatialHashGrid spatialHash(thickness * 2.0f);
    std::vector<SoftBodyParticleTrianglePair> candidatePairs;

    // Pairはiterationごとに変化するため、ParticleIndex/TriangleIndexの組をKeyにLambdaを保持します。
    std::unordered_map<uint64_t, float> lambdas;

    const float compliance = std::max(0.0f, settings.Compliance);
    const float alphaTilde = compliance / (deltaTime * deltaTime);
    const uint32_t iterationCount = std::max(1u, settings.SolverIterations);

    for (uint32_t iteration = 0u; iteration < iterationCount; ++iteration)
    {
        // Particle-Particleと同様、Position補正でTriangle AABBも変化します。
        // そのため各iterationでTriangleを最新Positionから再登録します。
        spatialHash.BuildTriangles(particles, triangles, thickness);
        spatialHash.GenerateParticleTriangleCandidates(particles, candidatePairs);

        for (const SoftBodyParticleTrianglePair& pair : candidatePairs)
        {
            if (pair.ParticleIndex >= particles.size() || pair.TriangleIndex >= triangles.size())
            {
                continue;
            }

            const SoftBodyTriangle& triangle = triangles[pair.TriangleIndex];
            if (TriangleContainsParticle(triangle, pair.ParticleIndex))
            {
                // 自分自身を構成頂点として含むTriangleとの距離は常に0になり得ます。
                // これは自己衝突ではなくCloth自身のTopologyなので必ず除外します。
                continue;
            }

            if (triangle.ParticleA >= particles.size()
                || triangle.ParticleB >= particles.size()
                || triangle.ParticleC >= particles.size())
            {
                continue;
            }

            SoftBodyParticle& particle = particles[pair.ParticleIndex];
            SoftBodyParticle& particleA = particles[triangle.ParticleA];
            SoftBodyParticle& particleB = particles[triangle.ParticleB];
            SoftBodyParticle& particleC = particles[triangle.ParticleC];

            const ClosestPointResult closest = ComputeClosestPointOnTriangle(
                particle.Position,
                particleA.Position,
                particleB.Position,
                particleC.Position);

            const math::Vec3 delta = particle.Position - closest.Point;
            const float distanceSq = delta.LengthSq();

            float distance = 0.0f;
            math::Vec3 normal{ 0.0f, 0.0f, 1.0f };
            if (distanceSq > math::Epsilon * math::Epsilon)
            {
                distance = std::sqrt(distanceSq);
                normal = delta / distance;
            }
            else
            {
                // ParticleがTriangle面上へ完全に一致した場合は距離Vectorから法線を作れません。
                // Triangle面法線をfallbackとし、退化Triangleなら固定+Zを維持してNaNを防ぎます。
                const math::Vec3 triangleNormal = math::Vec3::Cross(
                    particleB.Position - particleA.Position,
                    particleC.Position - particleA.Position);
                const float triangleNormalLengthSq = triangleNormal.LengthSq();
                if (triangleNormalLengthSq > math::Epsilon * math::Epsilon)
                {
                    normal = triangleNormal / std::sqrt(triangleNormalLengthSq);

                    // 直前位置が面の反対側にあった場合は、その側へ戻すよう法線向きを選びます。
                    const float previousSide = math::Vec3::Dot(
                        particle.PreviousPosition - closest.Point,
                        normal);
                    if (previousSide < 0.0f)
                    {
                        normal *= -1.0f;
                    }
                }
            }

            // Particle-Triangleの片側Thickness Constraint:
            //   C(x) = |particle - closestPoint(triangle)| - thickness >= 0
            const float constraintValue = distance - thickness;
            const uint64_t pairKey = MakeParticleTriangleKey(pair.ParticleIndex, pair.TriangleIndex);
            float& lambda = lambdas[pairKey];

            if (constraintValue >= 0.0f && lambda <= 0.0f)
            {
                continue;
            }

            // closestPoint = wA*A + wB*B + wC*C とみなし、
            // Gradientは Particle=+n, Triangle頂点=-weight*n としてXPBD denominatorを作ります。
            const float denominator =
                particle.InverseMass
                + particleA.InverseMass * closest.WeightA * closest.WeightA
                + particleB.InverseMass * closest.WeightB * closest.WeightB
                + particleC.InverseMass * closest.WeightC * closest.WeightC
                + alphaTilde;

            if (denominator <= math::Epsilon)
            {
                continue;
            }

            const float unconstrainedDeltaLambda =
                (-constraintValue - alphaTilde * lambda) / denominator;

            const float oldLambda = lambda;
            const float newLambda = std::max(0.0f, oldLambda + unconstrainedDeltaLambda);
            const float appliedDeltaLambda = newLambda - oldLambda;
            lambda = newLambda;

            if (std::abs(appliedDeltaLambda) <= math::Epsilon)
            {
                continue;
            }

            if (particle.IsFixed() == false)
            {
                particle.Position += normal * (particle.InverseMass * appliedDeltaLambda);
            }

            if (particleA.IsFixed() == false)
            {
                particleA.Position -= normal
                    * (particleA.InverseMass * closest.WeightA * appliedDeltaLambda);
            }

            if (particleB.IsFixed() == false)
            {
                particleB.Position -= normal
                    * (particleB.InverseMass * closest.WeightB * appliedDeltaLambda);
            }

            if (particleC.IsFixed() == false)
            {
                particleC.Position -= normal
                    * (particleC.InverseMass * closest.WeightC * appliedDeltaLambda);
            }
        }
    }

    // Solver::Step()後の追加Position passなので、Particle-Particle自己衝突と同じくVelocityを再構築します。
    const float inverseDeltaTime = 1.0f / deltaTime;
    for (SoftBodyParticle& particle : particles)
    {
        if (particle.IsFixed())
        {
            particle.Velocity = math::Vec3{};
            continue;
        }

        particle.Velocity =
            (particle.Position - particle.PreviousPosition) * inverseDeltaTime;
    }
}

} // namespace ph
} // namespace Raven
