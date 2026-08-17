#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// Soft Body Particle
// ============================================================================
// XPBDで扱う最小の質点です。
//
// RigidBodyとは異なり回転・慣性テンソルを持たず、Positionだけを制約によって補正します。
// PreviousPositionはStep開始時の位置を保持し、制約解決後のPositionとの差からVelocityを
// 再構築するために使用します。
struct SoftBodyParticle
{
    math::Vec3 Position{};
    math::Vec3 PreviousPosition{};
    math::Vec3 Velocity{};

    // 逆質量です。0.0f のParticleは固定点として扱います。
    // XPBDでは制約補正量をInverseMassで重み付けするため、固定点を特別なConstraintに
    // 分岐させず同じSolver式の中で扱えます。
    float InverseMass = 1.0f;

    bool IsFixed() const
    {
        return InverseMass <= 0.0f;
    }
};

} // namespace ph
} // namespace Raven
