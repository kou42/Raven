#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Raven
{
namespace math
{

    constexpr float Epsilon = 1e-6f;
    constexpr float Pi = 3.14159265358979323846f;

    constexpr float ToRadians(float deg)
    {
        return deg * Pi / 180.0f;
    }

    constexpr float ToDegrees(float rad)
    {
        return rad * 180.0f / Pi;
    }

    inline bool NearlyEqual(float a, float b, float eps = Epsilon)
    {
        return std::fabs(a - b) <= eps;
    }

    /*
    {
        using Vec2 = glm::vec2;
        using Vec3 = glm::vec3;
        using Vec4 = glm::vec4;

        using Mat3 = glm::mat3;
        using Mat4 = glm::mat4;

        using Quat = glm::quat;
    }
    */
}
}