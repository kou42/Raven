#pragma once

#include "Raven/Math/MathMatrix.h"

namespace Raven::math
{
// row-major格納・列Vector。アフィン行列のみを受け付け、失敗時は出力を変更しません。
bool IsFiniteAffine(const Mat4& matrix);
bool TryInverseAffine(const Mat4& matrix, Mat4& inverse);
// T * Rx * Ry * Rz * S（radian）。Quaternion::FromEulerXYZのRz*Ry*Rxとは別の既存Scene規約です。
// Shear/ゼロScaleは拒否。反射はXの負Scaleへ正規化し、再構成可能なTRSだけを返します。
bool TryDecomposeTRS(const Mat4& matrix, Vec3& position, Vec3& rotation, Vec3& scale);
}
