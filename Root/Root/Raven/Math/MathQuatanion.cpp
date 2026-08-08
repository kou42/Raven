#include "Raven/Math/MathQuatanion.h"

namespace Raven
{
namespace math
{

Quat Quat::FromAxisAngle(const Vec3& axis, float rad)
{
	Vec3 n = axis.Normalized();
	float half = rad * 0.5f;
	float s = std::sin(half);
	float c = std::cos(half);
	return { n.x * s, n.y * s, n.z * s, c };
}

Quat Quat::FromEulerXYZ(float pitchX, float yawY, float rollZ)
{
	Quat qx = FromAxisAngle({ 1, 0, 0 }, pitchX);
	Quat qy = FromAxisAngle({ 0, 1, 0 }, yawY);
	Quat qz = FromAxisAngle({ 0, 0, 1 }, rollZ);
	return qz * qy * qx;
}

float Quat::LengthSq() const
{
	return x * x + y * y + z * z + w * w;
}

float Quat::Length() const
{
	return std::sqrt(LengthSq());
}

Quat Quat::Normalized(float eps) const
{
	float len = Length();
	if (len <= eps) return Identity();
	return { x / len, y / len, z / len, w / len };
}

void Quat::Normalize(float eps)
{
	*this = Normalized(eps);
}

Quat Quat::Inversed(float eps) const
{
	float ls = LengthSq();
	assert(ls > eps);
	return Conjugate() * (1.0f / ls);
}

Vec3 Quat::Rotate(const Vec3& v) const
{
	Quat p{ v.x, v.y, v.z, 0.0f };
	Quat r = (*this) * p * this->Inversed();
	return { r.x, r.y, r.z };
}

Mat3 Quat::ToMat3() const
{
	Quat q = Normalized();

	float xx = q.x * q.x;
	float yy = q.y * q.y;
	float zz = q.z * q.z;
	float xy = q.x * q.y;
	float xz = q.x * q.z;
	float yz = q.y * q.z;
	float wx = q.w * q.x;
	float wy = q.w * q.y;
	float wz = q.w * q.z;

	return {
		1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz),        2.0f * (xz + wy),
		2.0f * (xy + wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx),
		2.0f * (xz - wy),        2.0f * (yz + wx),        1.0f - 2.0f * (xx + yy)
	};
}

Mat4 Quat::ToMat4() const
{
	Mat3 r = ToMat3();
	return {
		r[0][0], r[0][1], r[0][2], 0,
		r[1][0], r[1][1], r[1][2], 0,
		r[2][0], r[2][1], r[2][2], 0,
		0,       0,       0,       1
	};
}

float Quat::Dot(const Quat& a, const Quat& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

Quat Quat::Lerp(const Quat& a, const Quat& b, float t)
{
	return (a * (1.0f - t) + b * t).Normalized();
}

Quat Quat::Slerp(Quat a, Quat b, float t)
{
	float cosTheta = Dot(a, b);

	if (cosTheta < 0.0f)
	{
		b = b * -1.0f;
		cosTheta = -cosTheta;
	}

	if (cosTheta > 0.9995f)
		return Lerp(a, b, t);

	float theta = std::acos(cosTheta);
	float sinTheta = std::sin(theta);

	float wa = std::sin((1.0f - t) * theta) / sinTheta;
	float wb = std::sin(t * theta) / sinTheta;

	return (a * wa + b * wb).Normalized();
}

}
}
