#pragma once

#include "Raven/Math/Math.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Math/MathQuatanion.h"

namespace Raven
{
namespace math
{

inline Mat4 Translate(const Mat4& m, const Vec3& t)
{
    Mat4 trans = Mat4::Identity();

    trans.m[0][3] = t.x;
    trans.m[1][3] = t.y;
    trans.m[2][3] = t.z;

    return m * trans;
}

inline Mat4 Scale(const Mat4& m, const Vec3& s)
{
    Mat4 scale = Mat4::Identity();

    scale.m[0][0] = s.x;
    scale.m[1][1] = s.y;
    scale.m[2][2] = s.z;

    return m * scale;
}

inline Mat4 QuaternionToMatrix(const Quat& q)
{
    Mat4 m = Mat4::Identity();

    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;

    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;

    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    m.m[0][0] = 1.0f - 2.0f * (yy + zz);
    m.m[0][1] = 2.0f * (xy - wz);
    m.m[0][2] = 2.0f * (xz + wy);

    m.m[1][0] = 2.0f * (xy + wz);
    m.m[1][1] = 1.0f - 2.0f * (xx + zz);
    m.m[1][2] = 2.0f * (yz - wx);

    m.m[2][0] = 2.0f * (xz - wy);
    m.m[2][1] = 2.0f * (yz + wx);
    m.m[2][2] = 1.0f - 2.0f * (xx + yy);

    return m;
}

inline Mat4 Rotate(const Mat4& m, float radians, const Vec3& axis)
{
    // FromAxisAngle() は新しいQuaternionを戻り値として生成するstatic関数です。
    // q.FromAxisAngle(...) と呼ぶだけではq自身は変更されないため、戻り値を必ず受け取ります。
    //
    // この値を受け取らないとデフォルト構築されたIdentity Quaternionのままとなり、
    // TransformComponent::RotationやAnimationのRotationKeyを設定しても
    // 描画用Transform行列へ回転が反映されません。
    const Quat q = Quat::FromAxisAngle(axis, radians);

    const Mat4 rot = QuaternionToMatrix(q);

    return m * rot;
}

} // namespace math

} // namespace Raven