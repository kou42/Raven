#pragma once

#include <variant>

#include "Raven/Math/Math.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{

// ============================================================================
// UniformValue
// ============================================================================
// Material / Shader間で受け渡すUniform値の共通表現です。
// RendererAPI固有の型ではないためShader共通型として定義し、RHICommandListが
// Legacy RendererAPI.hへ依存せずUniformを受け取れるようにします。
using UniformValue = std::variant<
    int,
    float,
    math::Vec2,
    math::Vec3,
    math::Vec4,
    math::Mat4
>;

} // namespace Raven
