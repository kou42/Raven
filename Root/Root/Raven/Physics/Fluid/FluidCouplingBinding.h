#pragma once

#include <vector>

#include "Raven/Physics/Coupling/FluidRigidBodyCoupling.h"
#include "Raven/Physics/Coupling/FluidStaticColliderCoupling.h"
#include "Raven/Physics/Fluid/FluidParticle.h"

namespace Raven::ph
{

// ============================================================================
// FluidCouplingBinding
// ============================================================================
// Fluid Simulationが所有するParticle配列をFluidWorldのCoupling Phaseへ接続するための
// 非所有Bindingです。Particleの所有権はParticipant側に残し、FluidWorldはFixed Step境界で
// Static Collider / Dynamic RigidBodyとのCouplingを解決します。
//
// ParticipantごとにParticle RadiusやDrag等が異なる可能性があるため、Coupling設定はBindingへ
// 値として保持します。これにより共有Coupling Solverを利用しつつ設定の上書き順へ依存しません。
struct FluidCouplingBinding
{
    std::vector<FluidParticle>* Particles = nullptr;
    FluidStaticColliderCouplingSettings StaticColliderSettings{};
    FluidRigidBodyCouplingSettings RigidBodySettings{};
    bool StaticColliderCouplingEnabled = true;
    bool RigidBodyCouplingEnabled = true;
};

} // namespace Raven::ph
