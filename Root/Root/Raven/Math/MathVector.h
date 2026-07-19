#pragma once

#include "Raven/Math/Math.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Raven
{
namespace math
{

//============================================================
// Vec2
//============================================================
struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    explicit constexpr Vec2(float v) : x(v), y(v) {}

    float& operator[](int i)
    {
        assert(i >= 0 && i < 2);
        return (&x)[i];
    }

    const float& operator[](int i) const
    {
        assert(i >= 0 && i < 2);
        return (&x)[i];
    }

    constexpr Vec2 operator+() const { return *this; }
    constexpr Vec2 operator-() const { return { -x, -y }; }

    constexpr Vec2 operator+(const Vec2& v) const { return { x + v.x, y + v.y }; }
    constexpr Vec2 operator-(const Vec2& v) const { return { x - v.x, y - v.y }; }
    constexpr Vec2 operator*(float s) const { return { x * s, y * s }; }
    constexpr Vec2 operator/(float s) const { return { x / s, y / s }; }

    Vec2& operator+=(const Vec2& v) { x += v.x; y += v.y; return *this; }
    Vec2& operator-=(const Vec2& v) { x -= v.x; y -= v.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    Vec2& operator/=(float s) { x /= s; y /= s; return *this; }

    constexpr bool operator==(const Vec2& v) const { return x == v.x && y == v.y; }
    constexpr bool operator!=(const Vec2& v) const { return !(*this == v); }

    constexpr float LengthSq() const { return x * x + y * y; }
    float Length() const { return std::sqrt(LengthSq()); }

    Vec2 Normalized(float eps = Epsilon) const
    {
        float len = Length();
        if (len <= eps) return Vec2{};
        return *this / len;
    }

    void Normalize(float eps = Epsilon)
    {
        *this = Normalized(eps);
    }

    static constexpr float Dot(const Vec2& a, const Vec2& b)
    {
        return a.x * b.x + a.y * b.y;
    }

    static constexpr float Cross(const Vec2& a, const Vec2& b)
    {
        return a.x * b.y - a.y * b.x;
    }

    static Vec2 Lerp(const Vec2& a, const Vec2& b, float t)
    {
        return a * (1.0f - t) + b * t;
    }
};

constexpr Vec2 operator*(float s, const Vec2 & v)
{
    return v * s;
}

//============================================================
// Vec3
//============================================================
struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit constexpr Vec3(float v) : x(v), y(v), z(v) {}
    constexpr Vec3(const Vec2& xy, float z_) : x(xy.x), y(xy.y), z(z_) {}

    float& operator[](int i)
    {
        assert(i >= 0 && i < 3);
        return (&x)[i];
    }

    const float& operator[](int i) const
    {
        assert(i >= 0 && i < 3);
        return (&x)[i];
    }

    constexpr Vec3 operator+() const { return *this; }
    constexpr Vec3 operator-() const { return { -x, -y, -z }; }

    constexpr Vec3 operator+(const Vec3& v) const { return { x + v.x, y + v.y, z + v.z }; }
    constexpr Vec3 operator-(const Vec3& v) const { return { x - v.x, y - v.y, z - v.z }; }
    constexpr Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
    constexpr Vec3 operator/(float s) const { return { x / s, y / s, z / s }; }

    Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vec3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }

    constexpr bool operator==(const Vec3& v) const { return x == v.x && y == v.y && z == v.z; }
    constexpr bool operator!=(const Vec3& v) const { return !(*this == v); }

    constexpr float LengthSq() const { return x * x + y * y + z * z; }
    float Length() const { return std::sqrt(LengthSq()); }

    Vec3 Normalized(float eps = Epsilon) const
    {
        float len = Length();
        if (len <= eps) return Vec3{};
        return *this / len;
    }

    void Normalize(float eps = Epsilon)
    {
        *this = Normalized(eps);
    }

    static constexpr float Dot(const Vec3& a, const Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static constexpr Vec3 Cross(const Vec3& a, const Vec3& b)
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    static Vec3 Lerp(const Vec3& a, const Vec3& b, float t)
    {
        return a * (1.0f - t) + b * t;
    }
};

constexpr Vec3 operator*(float s, const Vec3 & v)
{
    return v * s;
}

//============================================================
// Vec4
//============================================================
struct Vec4
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    explicit constexpr Vec4(float v) : x(v), y(v), z(v), w(v) {}
    constexpr Vec4(const Vec3& xyz, float w_) : x(xyz.x), y(xyz.y), z(xyz.z), w(w_) {}

    float& operator[](int i)
    {
        assert(i >= 0 && i < 4);
        return (&x)[i];
    }

    const float& operator[](int i) const
    {
        assert(i >= 0 && i < 4);
        return (&x)[i];
    }

    constexpr Vec4 operator+() const { return *this; }
    constexpr Vec4 operator-() const { return { -x, -y, -z, -w }; }

    constexpr Vec4 operator+(const Vec4& v) const { return { x + v.x, y + v.y, z + v.z, w + v.w }; }
    constexpr Vec4 operator-(const Vec4& v) const { return { x - v.x, y - v.y, z - v.z, w - v.w }; }
    constexpr Vec4 operator*(float s) const { return { x * s, y * s, z * s, w * s }; }
    constexpr Vec4 operator/(float s) const { return { x / s, y / s, z / s, w / s }; }

    Vec4& operator+=(const Vec4& v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
    Vec4& operator-=(const Vec4& v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
    Vec4& operator*=(float s) { x *= s; y *= s; z *= s; w *= s; return *this; }
    Vec4& operator/=(float s) { x /= s; y /= s; z /= s; w /= s; return *this; }

    constexpr bool operator==(const Vec4& v) const { return x == v.x && y == v.y && z == v.z && w == v.w; }
    constexpr bool operator!=(const Vec4& v) const { return !(*this == v); }

    constexpr float LengthSq() const { return x * x + y * y + z * z + w * w; }
    float Length() const { return std::sqrt(LengthSq()); }

    Vec4 Normalized(float eps = Epsilon) const
    {
        float len = Length();
        if (len <= eps) return Vec4{};
        return *this / len;
    }

    static constexpr float Dot(const Vec4& a, const Vec4& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }
};

constexpr Vec4 operator*(float s, const Vec4 & v)
{
    return v * s;
}

} // math
} // raven