#pragma once

#include "Math.h"
#include "MathVector.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Raven
{
namespace math
{

//struct Vec2;
//struct Vec3;
//struct Vec4;

//============================================================
// Mat3 : row-major
//============================================================
struct Mat3
{

    float m[3][3]{};

    constexpr Mat3() = default;

    constexpr Mat3(
        float m00, float m01, float m02,
        float m10, float m11, float m12,
        float m20, float m21, float m22)
        : m{ {m00, m01, m02}, {m10, m11, m12}, {m20, m21, m22} }
    {
    }

    static constexpr Mat3 Identity()
    {
        return {
            1, 0, 0,
            0, 1, 0,
            0, 0, 1
        };
    }


    float* operator[](int r)
    {
        assert(r >= 0 && r < 3);
        return m[r];
    }

    const float* operator[](int r) const
    {
        assert(r >= 0 && r < 3);
        return m[r];
    }

    Mat3 operator+(const Mat3& o) const
    {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = m[i][j] + o.m[i][j];
        return r;
    }

    Mat3 operator-(const Mat3& o) const
    {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = m[i][j] - o.m[i][j];
        return r;
    }

    Mat3 operator*(float s) const
    {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = m[i][j] * s;
        return r;
    }

    Vec3 operator*(const Vec3& v) const
    {
        return {
            m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z
        };
    }

    Mat3 operator*(const Mat3& o) const
    {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                r.m[i][j] = 0.0f;
                for (int k = 0; k < 3; ++k)
                    r.m[i][j] += m[i][k] * o.m[k][j];
            }
        }
        return r;
    }

    Mat3 Transposed() const
    {
        return {
            m[0][0], m[1][0], m[2][0],
            m[0][1], m[1][1], m[2][1],
            m[0][2], m[1][2], m[2][2]
        };
    }

    float Determinant() const
    {
        return
            m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
            m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
            m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }

    Mat3 Inversed(float eps = Epsilon) const
    {
        float det = Determinant();
        assert(std::fabs(det) > eps);
        float invDet = 1.0f / det;

        Mat3 r;
        r[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * invDet;
        r[0][1] = -(m[0][1] * m[2][2] - m[0][2] * m[2][1]) * invDet;
        r[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * invDet;

        r[1][0] = -(m[1][0] * m[2][2] - m[1][2] * m[2][0]) * invDet;
        r[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * invDet;
        r[1][2] = -(m[0][0] * m[1][2] - m[0][2] * m[1][0]) * invDet;

        r[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * invDet;
        r[2][1] = -(m[0][0] * m[2][1] - m[0][1] * m[2][0]) * invDet;
        r[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * invDet;
        return r;
    }

};

//============================================================
// Mat4 : row-major, column vector multiplication style
//============================================================
struct Mat4
{
    float m[4][4]{};

    constexpr Mat4() = default;

    constexpr Mat4(
        float m00, float m01, float m02, float m03,
        float m10, float m11, float m12, float m13,
        float m20, float m21, float m22, float m23,
        float m30, float m31, float m32, float m33)
        : m{ {m00, m01, m02, m03}, {m10, m11, m12, m13}, {m20, m21, m22, m23}, {m30, m31, m32, m33} }
    {
    }

    static constexpr Mat4 Identity()
    {
        return {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
    }

    float* operator[](int r)
    {
        assert(r >= 0 && r < 4);
        return m[r];
    }

    const float* operator[](int r) const
    {
        assert(r >= 0 && r < 4);
        return m[r];
    }

    Vec4 operator*(const Vec4& v) const
    {
        return {
            m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3] * v.w,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3] * v.w,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] * v.w,
            m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3] * v.w
        };
    }

    Mat4 operator*(const Mat4& o) const
    {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                r.m[i][j] = 0.0f;
                for (int k = 0; k < 4; ++k)
                    r.m[i][j] += m[i][k] * o.m[k][j];
            }
        }
        return r;
    }

    Mat4 Transposed() const
    {
        return {
            m[0][0], m[1][0], m[2][0], m[3][0],
            m[0][1], m[1][1], m[2][1], m[3][1],
            m[0][2], m[1][2], m[2][2], m[3][2],
            m[0][3], m[1][3], m[2][3], m[3][3]
        };
    }

    static constexpr Mat4 Translation(const Vec3& t)
    {
        return {
            1, 0, 0, t.x,
            0, 1, 0, t.y,
            0, 0, 1, t.z,
            0, 0, 0, 1
        };
    }

    static constexpr Mat4 Scaling(const Vec3& s)
    {
        return {
            s.x, 0,   0,   0,
            0,   s.y, 0,   0,
            0,   0,   s.z, 0,
            0,   0,   0,   1
        };
    }

    static Mat4 RotationX(float rad)
    {
        float c = std::cos(rad);
        float s = std::sin(rad);
        return {
            1, 0,  0, 0,
            0, c, -s, 0,
            0, s,  c, 0,
            0, 0,  0, 1
        };
    }

    static Mat4 RotationY(float rad)
    {
        float c = std::cos(rad);
        float s = std::sin(rad);
        return {
             c, 0, s, 0,
             0, 1, 0, 0,
            -s, 0, c, 0,
             0, 0, 0, 1
        };
    }

    static Mat4 RotationZ(float rad)
    {
        float c = std::cos(rad);
        float s = std::sin(rad);
        return {
            c, -s, 0, 0,
            s,  c, 0, 0,
            0,  0, 1, 0,
            0,  0, 0, 1
        };
    }

    static Mat4 Perspective(float fovyRad, float aspect, float nearZ, float farZ)
    {
        float f = 1.0f / std::tan(fovyRad * 0.5f);
        float nf = 1.0f / (nearZ - farZ);

        return {
            f / aspect, 0, 0,                         0,
            0,          f, 0,                         0,
            0,          0, (farZ + nearZ) * nf,       2.0f * farZ * nearZ * nf,
            0,          0, -1,                        0
        };
    }

    static Mat4 Orthographic(float left, float right, float bottom, float top, float nearZ, float farZ)
    {
        return {
            2.0f / (right - left), 0, 0, -(right + left) / (right - left),
            0, 2.0f / (top - bottom), 0, -(top + bottom) / (top - bottom),
            0, 0, -2.0f / (farZ - nearZ), -(farZ + nearZ) / (farZ - nearZ),
            0, 0, 0, 1
        };
    }

    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
    {
        Vec3 f = (target - eye).Normalized();
        Vec3 r = Vec3::Cross(f, up).Normalized();
        Vec3 u = Vec3::Cross(r, f);

        return {
            r.x,  r.y,  r.z, -Vec3::Dot(r, eye),
            u.x,  u.y,  u.z, -Vec3::Dot(u, eye),
           -f.x, -f.y, -f.z,  Vec3::Dot(f, eye),
            0,    0,    0,    1
        };
    }
};





} // math
} // raven