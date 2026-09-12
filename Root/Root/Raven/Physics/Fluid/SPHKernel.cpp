#include "Raven/Physics/Fluid/SPHKernel.h"

#include <algorithm>

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

} // namespace ph
} // namespace Raven
