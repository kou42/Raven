#include "Raven/Physics/Fluid/SPHKernel.h"

#include "Raven/Math/Math.h"

namespace Raven
{
namespace ph
{

SPHKernel::SPHKernel(float smoothingRadius)
    : m_SmoothingRadius(smoothingRadius)
    , m_SmoothingRadiusSq(smoothingRadius * smoothingRadius)
{
    if (smoothingRadius <= math::Epsilon)
    {
        return;
    }

    // support radiusが固定の間は同じ係数を使い、近傍ごとの累乗と除算を省きます。
    const float h3 = m_SmoothingRadiusSq * smoothingRadius;
    const float h6 = h3 * h3;
    const float h9 = h6 * h3;
    m_Poly6Coefficient = 315.0f / (64.0f * math::Pi * h9);
    m_SpikyCoefficient = -45.0f / (math::Pi * h6);
}

float SPHKernel::EvaluatePoly6Density(float distanceSq) const
{
    if (m_SmoothingRadius <= math::Epsilon
        || distanceSq < 0.0f || distanceSq > m_SmoothingRadiusSq)
    {
        return 0.0f;
    }

    const float h2MinusR2 = m_SmoothingRadiusSq - distanceSq;

    // pow()を使わず3乗を明示し、Kernel式と計算コストを読み取りやすくします。
    return m_Poly6Coefficient * h2MinusR2 * h2MinusR2 * h2MinusR2;
}

math::Vec3 SPHKernel::EvaluateSpikyGradient(
    const math::Vec3& displacement,
    float distance) const
{
    if (m_SmoothingRadius <= math::Epsilon
        || distance <= math::Epsilon
        || distance > m_SmoothingRadius)
    {
        return math::Vec3{};
    }

    // displacement / distance が rHat です。中心r=0では方向が定義できないため上で0を返します。
    return EvaluateSpikyGradientInSupport(displacement, distance, m_SmoothingRadius - distance);
}

math::Vec3 SPHKernel::EvaluateSpikyGradientInSupport(
    const math::Vec3& displacement, float distance, float remainingRadius) const
{
    return displacement * (m_SpikyCoefficient * remainingRadius * remainingRadius / distance);
}

float SPHKernel::EvaluateViscosityLaplacian(float distance) const
{
    if (m_SmoothingRadius <= math::Epsilon
        || distance < 0.0f
        || distance > m_SmoothingRadius)
    {
        return 0.0f;
    }

    return EvaluateViscosityLaplacianInSupport(m_SmoothingRadius - distance);
}

float SPHKernel::EvaluateViscosityLaplacianInSupport(float remainingRadius) const
{
    return -m_SpikyCoefficient * remainingRadius;
}

float SPHKernel::EvaluatePoly6Density(float distanceSq, float smoothingRadius)
{
    return SPHKernel(smoothingRadius).EvaluatePoly6Density(distanceSq);
}

math::Vec3 SPHKernel::EvaluateSpikyGradient(
    const math::Vec3& displacement, float distance, float smoothingRadius)
{
    return SPHKernel(smoothingRadius).EvaluateSpikyGradient(displacement, distance);
}

float SPHKernel::EvaluateViscosityLaplacian(float distance, float smoothingRadius)
{
    return SPHKernel(smoothingRadius).EvaluateViscosityLaplacian(distance);
}

} // namespace ph
} // namespace Raven
