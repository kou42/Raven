#pragma once

#include <cmath>

#include "Raven/Physics/Field/PhysicsField.h"

namespace Raven::ph
{

// ============================================================================
// GravityField
// ============================================================================
// 重力場を表すVectorFieldです。
// Evaluate()の戻り値はForceそのものではなく、world-spaceの重力加速度 [m/s^2] です。
// Dynamic RigidBodyでは質量に依存せず同じ加速度として積分されます。
class GravityField : public VectorField
{
public:
    ~GravityField() override = default;
};

// ============================================================================
// UniformGravityField
// ============================================================================
// 空間上のどの位置でも同じ重力加速度を返す一様重力場です。
// 従来PhysicsWorld::m_Gravityが表していた契約をField Architectureへ移した型で、
// SetGravity() / GetGravity()の既存APIはPhysicsWorld側でこの値へ委譲します。
class UniformGravityField final : public GravityField
{
public:
    UniformGravityField() = default;

    explicit UniformGravityField(const math::Vec3& acceleration)
        : m_Acceleration(acceleration)
    {
    }

    math::Vec3 Evaluate(const math::Vec3& worldPosition) const override
    {
        static_cast<void>(worldPosition);
        return m_Acceleration;
    }

    void SetAcceleration(const math::Vec3& acceleration)
    {
        m_Acceleration = acceleration;
    }

    math::Vec3& GetAcceleration()
    {
        return m_Acceleration;
    }

    const math::Vec3& GetAcceleration() const
    {
        return m_Acceleration;
    }

private:
    math::Vec3 m_Acceleration{ 0.0f, -9.80665f, 0.0f };
};

// ============================================================================
// PointGravityField
// ============================================================================
// 1点へ向かう逆二乗則の重力場です。
// Strengthは G*M に相当する重力パラメータ [m^3/s^2] として扱い、Evaluate()は
//   g(x) = -Strength * (x - Center) / |x - Center|^3
// を返します。RigidBodyの質量はここでは掛けず、GravityField共通契約どおり加速度を返します。
//
// 中心近傍では理想的な点質量の式が発散するため、MinDistanceより内側では距離をClampします。
// これはNaN/InfによるSimulation全体の破綻を避けるための数値安全策です。
class PointGravityField final : public GravityField
{
public:
    PointGravityField() = default;

    PointGravityField(const math::Vec3& center, float strength, float minDistance = 0.01f)
        : m_Center(center)
        , m_Strength(strength)
        , m_MinDistance(minDistance)
    {
    }

    math::Vec3 Evaluate(const math::Vec3& worldPosition) const override
    {
        const math::Vec3 offset = worldPosition - m_Center;
        const float distanceSquared = offset.LengthSq();
        if (distanceSquared <= 1.0e-12f)
        {
            // 中心では方向自体が定義できないため、有限なゼロ加速度を返します。
            return {};
        }

        const float distance = std::sqrt(distanceSquared);
        const float safeMinDistance = m_MinDistance > 0.0f ? m_MinDistance : 0.0f;
        const float effectiveDistance = distance < safeMinDistance ? safeMinDistance : distance;
        if (effectiveDistance <= 1.0e-6f)
        {
            return {};
        }

        const math::Vec3 directionFromCenter = offset / distance;
        const float accelerationMagnitude = m_Strength / (effectiveDistance * effectiveDistance);
        return directionFromCenter * -accelerationMagnitude;
    }

    void SetCenter(const math::Vec3& center) { m_Center = center; }
    const math::Vec3& GetCenter() const { return m_Center; }

    void SetStrength(float strength) { m_Strength = strength; }
    float GetStrength() const { return m_Strength; }

    void SetMinDistance(float minDistance) { m_MinDistance = minDistance; }
    float GetMinDistance() const { return m_MinDistance; }

private:
    math::Vec3 m_Center{};
    float m_Strength = 9.80665f;
    float m_MinDistance = 0.01f;
};

} // namespace Raven::ph
