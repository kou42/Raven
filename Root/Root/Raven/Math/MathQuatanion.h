#pragma once

#include "Raven/Math/Math.h"
//#include "MathVector.h"
#include "Raven/Math/MathMatrix.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Raven
{
namespace math
{

struct Quat
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static constexpr Quat Identity()
    {
        return { 0, 0, 0, 1 };
    }

    static Quat FromAxisAngle(const Vec3& axis, float rad);

    static Quat FromEulerXYZ(float pitchX, float yawY, float rollZ);

    // FromEulerXYZ()と同じ回転順序(qz * qy * qx)に対応するEuler角へ戻します。
    // SceneのTransformComponentが現在Euler角を保持しているため、Animation側でQuaternionを
    // 正規表現として使いつつ、Scene境界で互換形式へ変換するために利用します。
    Vec3 ToEulerXYZ() const;

    constexpr Quat operator*(const Quat& q) const
    {
        return {
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w,
            w * q.w - x * q.x - y * q.y - z * q.z
        };
    }

    constexpr Quat operator*(float s) const
    {
        return { x * s, y * s, z * s, w * s };
    }

    constexpr Quat operator+(const Quat& q) const
    {
        return { x + q.x, y + q.y, z + q.z, w + q.w };
    }

    float LengthSq() const;

    float Length() const;

    Quat Normalized(float eps = Epsilon) const;

    void Normalize(float eps = Epsilon);

    constexpr Quat Conjugate() const
    {
        return { -x, -y, -z, w };
    }

    Quat Inversed(float eps = Epsilon) const;

    Vec3 Rotate(const Vec3& v) const;

    Mat3 ToMat3() const;

    Mat4 ToMat4() const;

    static float Dot(const Quat& a, const Quat& b);

    static Quat Lerp(const Quat& a, const Quat& b, float t);

    static Quat Slerp(Quat a, Quat b, float t);
};

}
}