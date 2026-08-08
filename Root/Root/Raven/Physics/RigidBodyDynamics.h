#pragma once

#include <algorithm>
#include <cmath>

#include "Raven/Math/MathMatrix.h"
#include "Raven/Physics/Collision/OBB.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{

// ============================================================================
// Rigid body inertia helpers
// ============================================================================
// 慣性テンソルは「質量が回転に対してどれだけ動きにくいか」を表します。
// Solverでは逆テンソル I^-1 を使うことで、角Impulse
//
//     deltaOmega = I_world^-1 * (r x J)
//
// を直接計算できます。
//
// 現段階ではColliderを剛体形状そのものとみなし、Box / Sphereの解析解を使用します。
// Static / Kinematicは衝突Impulseで角速度を変更しないためzero matrixを返します。
inline math::Mat3 ComputeLocalInverseInertia(
    const RigidBodyComponent* body,
    const ColliderComponent* collider)
{
    math::Mat3 inverse{};
    if (body == nullptr || collider == nullptr || body->Type != BodyType::Dynamic
        || body->Mass <= 0.0f || body->InverseMass <= 0.0f)
    {
        return inverse;
    }

    if (collider->Type == ColliderType::Box)
    {
        const float hx = std::abs(collider->HalfExtents.x);
        const float hy = std::abs(collider->HalfExtents.y);
        const float hz = std::abs(collider->HalfExtents.z);

        // Full size = 2h を直方体の慣性公式へ代入すると
        // Ixx = (1/3)m(hy^2 + hz^2) になります。
        const float ixx = (body->Mass / 3.0f) * (hy * hy + hz * hz);
        const float iyy = (body->Mass / 3.0f) * (hx * hx + hz * hz);
        const float izz = (body->Mass / 3.0f) * (hx * hx + hy * hy);
        inverse[0][0] = ixx > 1.0e-12f ? 1.0f / ixx : 0.0f;
        inverse[1][1] = iyy > 1.0e-12f ? 1.0f / iyy : 0.0f;
        inverse[2][2] = izz > 1.0e-12f ? 1.0f / izz : 0.0f;
    }
    else if (collider->Type == ColliderType::Sphere)
    {
        const float radius = std::max(collider->Radius, 0.0f);
        // Solid sphere: I = 2/5 m r^2
        const float inertia = 0.4f * body->Mass * radius * radius;
        const float value = inertia > 1.0e-12f ? 1.0f / inertia : 0.0f;
        inverse[0][0] = value;
        inverse[1][1] = value;
        inverse[2][2] = value;
    }

    return inverse;
}

inline math::Mat3 ComputeWorldInverseInertia(
    const TransformComponent* transform,
    const RigidBodyComponent* body,
    const ColliderComponent* collider)
{
    const math::Mat3 localInverse = ComputeLocalInverseInertia(body, collider);
    if (transform == nullptr || collider == nullptr || body == nullptr || body->Type != BodyType::Dynamic)
    {
        return localInverse;
    }

    // Sphereは全方向で同じ慣性なので回転変換しても値は変わりません。
    if (collider->Type != ColliderType::Box)
    {
        return localInverse;
    }

    OBB obb{};
    if (!ComputeBoxOBB(*transform, *collider, obb))
    {
        return math::Mat3{};
    }

    // Rのcolumnにbody-local basisを並べ、
    //     I_world^-1 = R * I_local^-1 * R^T
    // でworld-space tensorへ変換します。
    const math::Mat3 rotation{
        obb.Axis[0].x, obb.Axis[1].x, obb.Axis[2].x,
        obb.Axis[0].y, obb.Axis[1].y, obb.Axis[2].y,
        obb.Axis[0].z, obb.Axis[1].z, obb.Axis[2].z
    };
    return rotation * localInverse * rotation.Transposed();
}

inline math::Vec3 GetVelocityAtPoint(
    const RigidBodyComponent* body,
    const TransformComponent* transform,
    const math::Vec3& worldPoint)
{
    if (body == nullptr || transform == nullptr)
    {
        return math::Vec3{};
    }

    const math::Vec3 r = worldPoint - transform->Position;
    return body->LinearVelocity + math::Vec3::Cross(body->AngularVelocity, r);
}

} // namespace Raven::ph
