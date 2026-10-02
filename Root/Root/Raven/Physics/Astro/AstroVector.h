#pragma once

#include <cmath>

#include "Raven/Math/MathVector.h"

namespace Raven::ph
{

// Astro Solver内部で絶対座標・速度・Forceを保持するdouble precision vectorです。
// Scene/Rendererのfloat座標とはAstroWorld境界でのみ変換し、長距離計算の丸め誤差を局所化します。
struct AstroVector3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr AstroVector3() = default;
    constexpr AstroVector3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    explicit constexpr AstroVector3(const math::Vec3& value)
        : x(static_cast<double>(value.x)),
          y(static_cast<double>(value.y)),
          z(static_cast<double>(value.z))
    {
    }

    constexpr AstroVector3 operator-() const { return { -x, -y, -z }; }
    constexpr AstroVector3 operator+(const AstroVector3& value) const
    {
        return { x + value.x, y + value.y, z + value.z };
    }
    constexpr AstroVector3 operator-(const AstroVector3& value) const
    {
        return { x - value.x, y - value.y, z - value.z };
    }
    constexpr AstroVector3 operator*(double scalar) const
    {
        return { x * scalar, y * scalar, z * scalar };
    }
    constexpr AstroVector3 operator/(double scalar) const
    {
        return { x / scalar, y / scalar, z / scalar };
    }

    AstroVector3& operator+=(const AstroVector3& value)
    {
        x += value.x;
        y += value.y;
        z += value.z;
        return *this;
    }
    AstroVector3& operator-=(const AstroVector3& value)
    {
        x -= value.x;
        y -= value.y;
        z -= value.z;
        return *this;
    }

    constexpr double LengthSq() const { return x * x + y * y + z * z; }
    double Length() const { return std::sqrt(LengthSq()); }

    static constexpr double Dot(const AstroVector3& a, const AstroVector3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static constexpr AstroVector3 Cross(const AstroVector3& a, const AstroVector3& b)
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    math::Vec3 ToSceneVector() const
    {
        return {
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(z)
        };
    }
};

constexpr AstroVector3 operator*(double scalar, const AstroVector3& value)
{
    return value * scalar;
}

} // namespace Raven::ph
