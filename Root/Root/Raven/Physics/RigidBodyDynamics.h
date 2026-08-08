#pragma once

#include <algorithm>
#include <cmath>

#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathQuatanion.h"
#include "Raven/Physics/Collision/OBB.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{

// ============================================================================
// Orientation / Inertia Utility
// ============================================================================
// Physicsでは回転の正本をQuaternionで保持し、TransformのEulerは表示互換用に同期します。
// これにより積分時の数値安定性を保ちつつ、既存Inspectorとの互換性も維持します。

// Transform::GetTransform()の回転順 Rx * Ry * Rz と同じ姿勢をQuaternionで作ります。
// 既存Quat::FromEulerXYZ()は別の積順なので、Physicsでは明示的にこちらを使います。
inline math::Quat PhysicsOrientationFromEuler(const math::Vec3& euler)
{
    const math::Quat qx = math::Quat::FromAxisAngle({ 1.0f, 0.0f, 0.0f }, euler.x);
    const math::Quat qy = math::Quat::FromAxisAngle({ 0.0f, 1.0f, 0.0f }, euler.y);
    const math::Quat qz = math::Quat::FromAxisAngle({ 0.0f, 0.0f, 1.0f }, euler.z);
    return (qx * qy * qz).Normalized();
}

// QuaternionをTransform互換の Rx * Ry * Rz Eulerへ戻します。
// Physicsの正規姿勢はQuaternion側なので、このEulerは描画・Inspector互換用のミラーです。
inline math::Vec3 PhysicsEulerFromOrientation(const math::Quat& orientation)
{
    const math::Mat3 m = orientation.Normalized().ToMat3();
    const float sy = std::clamp(m[0][2], -1.0f, 1.0f);
    const float y = std::asin(sy);
    const float cy = std::cos(y);

    if (std::abs(cy) > 1.0e-6f)
    {
        return {
            std::atan2(-m[1][2], m[2][2]),
            y,
            std::atan2(-m[0][1], m[0][0])
        };
    }

    // Gimbal lock近傍ではEuler表現そのものが一意ではありません。
    // z=0を選び、Quaternionの姿勢と等価な代表Eulerを返します。
    return { std::atan2(m[2][1], m[1][1]), y, 0.0f };
}

inline void EnsurePhysicsOrientation(
    const TransformComponent& transform,
    RigidBodyComponent& body)
{
    // 初回アクセス時だけTransform姿勢からQuaternionを初期化します。
    // 以後はQuaternionを正本として維持し、Eulerはそこから再計算します。
    if (!body.OrientationInitialized)
    {
        body.Orientation = PhysicsOrientationFromEuler(transform.Rotation);
        body.OrientationInitialized = true;
    }
}

inline void IntegratePhysicsOrientation(
    TransformComponent& transform,
    RigidBodyComponent& body,
    float dt)
{
    EnsurePhysicsOrientation(transform, body);
    if (dt <= 0.0f || body.AngularVelocity.LengthSq() <= 1.0e-16f)
    {
        transform.Rotation = PhysicsEulerFromOrientation(body.Orientation);
        return;
    }

    // AngularVelocityはworld-spaceです。
    // qDot = 1/2 * omegaQuat * q をsemi-implicit Eulerで積分し、毎Step正規化します。
    // 正規化を省くと長時間実行で基底が歪み、慣性計算や接触法線が不安定化します。
    const math::Quat omega{
        body.AngularVelocity.x,
        body.AngularVelocity.y,
        body.AngularVelocity.z,
        0.0f
    };
    body.Orientation = (body.Orientation + (omega * body.Orientation) * (0.5f * dt)).Normalized();
    transform.Rotation = PhysicsEulerFromOrientation(body.Orientation);
}

// ============================================================================
// Rigid body inertia helpers
// ============================================================================
// Box/Sphereの簡易慣性モデルを使い、動的剛体の逆慣性テンソルを返します。
// 本プロジェクトではTrigger/Static/Kinematicは拘束解法対象外としてゼロ行列を返します。
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
        // 直方体慣性: Ixx = m/3*(hy^2+hz^2) など（半長hx/hy/hz定義）。
        const float hx = std::abs(collider->HalfExtents.x);
        const float hy = std::abs(collider->HalfExtents.y);
        const float hz = std::abs(collider->HalfExtents.z);
        const float ixx = (body->Mass / 3.0f) * (hy * hy + hz * hz);
        const float iyy = (body->Mass / 3.0f) * (hx * hx + hz * hz);
        const float izz = (body->Mass / 3.0f) * (hx * hx + hy * hy);
        inverse[0][0] = ixx > 1.0e-12f ? 1.0f / ixx : 0.0f;
        inverse[1][1] = iyy > 1.0e-12f ? 1.0f / iyy : 0.0f;
        inverse[2][2] = izz > 1.0e-12f ? 1.0f / izz : 0.0f;
    }
    else if (collider->Type == ColliderType::Sphere)
    {
        // 球慣性: I = 2/5 * m * r^2
        const float radius = std::max(collider->Radius, 0.0f);
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
        return localInverse;
    if (collider->Type != ColliderType::Box)
        return localInverse;

    // Physics姿勢が初期化済みならQuaternionを直接使います。
    // 未初期化時だけTransform Eulerから構築することで、最初の衝突判定にも対応します。
    const math::Quat orientation = body->OrientationInitialized
        ? body->Orientation.Normalized()
        : PhysicsOrientationFromEuler(transform->Rotation);
    const math::Mat3 rotation = orientation.ToMat3();
    // ワールド逆慣性: Iw^-1 = R * Ilocal^-1 * R^T
    return rotation * localInverse * rotation.Transposed();
}

inline math::Vec3 GetVelocityAtPoint(
    const RigidBodyComponent* body,
    const TransformComponent* transform,
    const math::Vec3& worldPoint)
{
    // 剛体上の任意点速度 v = v_linear + omega x r
    if (body == nullptr || transform == nullptr)
        return math::Vec3{};
    const math::Vec3 r = worldPoint - transform->Position;
    return body->LinearVelocity + math::Vec3::Cross(body->AngularVelocity, r);
}

} // namespace Raven::ph
