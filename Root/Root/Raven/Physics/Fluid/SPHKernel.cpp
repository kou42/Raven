#include "Raven/Physics/Fluid/SPHKernel.h"

#include "Raven/Math/Math.h"

namespace Raven
{
namespace ph
{

float SPHKernel::EvaluatePoly6Density(float distanceSq, float smoothingRadius)
{
    if (smoothingRadius <= math::Epsilon)
    {
        return 0.0f;
    }

    const float smoothingRadiusSq = smoothingRadius * smoothingRadius;
    if (distanceSq < 0.0f || distanceSq > smoothingRadiusSq)
    {
        return 0.0f;
    }

    const float h2MinusR2 = smoothingRadiusSq - distanceSq;
    const float h3 = smoothingRadius * smoothingRadius * smoothingRadius;
    const float h6 = h3 * h3;
    const float h9 = h6 * h3;
    const float coefficient = 315.0f / (64.0f * math::Pi * h9);

    // pow()を使わず3乗を明示し、Kernel式と計算コストを読み取りやすくします。
    return coefficient * h2MinusR2 * h2MinusR2 * h2MinusR2;
}

math::Vec3 SPHKernel::EvaluateSpikyGradient(
    const math::Vec3& displacement,
    float distance,
    float smoothingRadius)
{
    if (smoothingRadius <= math::Epsilon
        || distance <= math::Epsilon
        || distance > smoothingRadius)
    {
        return math::Vec3{};
    }

    const float h2 = smoothingRadius * smoothingRadius;
    const float h3 = h2 * smoothingRadius;
    const float h6 = h3 * h3;
    const float hMinusR = smoothingRadius - distance;
    const float coefficient = -45.0f / (math::Pi * h6);

    // displacement / distance が rHat です。中心r=0では方向が定義できないため上で0を返します。
    return displacement * (coefficient * hMinusR * hMinusR / distance);
}

float SPHKernel::EvaluateViscosityLaplacian(
    float distance,
    float smoothingRadius)
{
    if (smoothingRadius <= math::Epsilon
        || distance < 0.0f
        || distance > smoothingRadius)
    {
        return 0.0f;
    }

    const float h2 = smoothingRadius * smoothingRadius;
    const float h3 = h2 * smoothingRadius;
    const float h6 = h3 * h3;
    return 45.0f / (math::Pi * h6) * (smoothingRadius - distance);
}

} // namespace ph
} // namespace Raven
