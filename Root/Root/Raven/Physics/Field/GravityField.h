#pragma once

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

    // PhysicsWorldの既存SetGravity()/GetGravity()実装を段階的に移行するため、
    // Fieldが所有する値へのmutable参照も提供します。新規コードではSetAcceleration()を優先します。
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

} // namespace Raven::ph
