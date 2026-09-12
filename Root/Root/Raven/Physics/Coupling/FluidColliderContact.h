#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Fluid/FluidParticle.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{

// ============================================================================
// Fluid Collider Contact
// ============================================================================
// Fluid Particleを半径付きSphereとしてColliderへ当てたときの幾何情報です。
// Static / Dynamicの応答方法はCoupling側で分け、接触生成自体は共通化します。
struct FluidColliderContact
{
    // Collider表面からFluid Particle側を向くworld-space法線です。
    math::Vec3 Normal{ 0.0f, 1.0f, 0.0f };

    // Collider表面上の代表接触点です。Dynamic RigidBodyへ反作用Impulseを返す際の
    // r x J の作用点として利用します。
    math::Vec3 Point{};

    // 貫通を解消したParticle中心位置です。
    math::Vec3 CorrectedParticlePosition{};

    float PenetrationDepth = 0.0f;
};

// 現段階ではSphere / Box Colliderを対象にします。
// 応答を含めない純粋な幾何Queryなので、Static / Dynamic Coupling双方から再利用できます。
bool GenerateFluidParticleColliderContact(
    const FluidParticle& particle,
    float particleRadius,
    const TransformComponent& transform,
    const ColliderComponent& collider,
    FluidColliderContact& outContact);

} // namespace Raven::ph
