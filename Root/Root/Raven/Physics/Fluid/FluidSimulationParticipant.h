#pragma once

#include "Raven/Physics/Fluid/FluidCouplingBinding.h"

namespace Raven::ph
{

// ============================================================================
// FluidSimulationParticipant
// ============================================================================
// Fluid Simulationの所有場所(Debug Layerや将来のFluid Component/System等)を
// Physics Domainから隠したまま、Fixed StepでSimulationを進め、その後に外部出力同期を
// 要求するための最小境界です。
//
// PhysicsSimulationWorldはSPH / PBF / FLIPなど具体的なSolver型へ依存せず、
// FluidWorldへ登録されたParticipantの実行順序だけを統括します。
class FluidSimulationParticipant
{
public:
    virtual ~FluidSimulationParticipant() = default;

    virtual void SimulateFluid(float fixedDeltaTime) = 0;

    // Scene/RigidBody Couplingを必要とするParticipantだけBindingを公開します。
    // Particle-based Couplingを使わない実装は既定nullptrのまま利用できます。
    virtual FluidCouplingBinding* GetFluidCouplingBinding()
    {
        return nullptr;
    }

    // Simulation参加者がRender EntityやGPU Bufferなど外部出力を持つ場合だけoverrideします。
    // 純粋なPhysics Test Participantは既定no-opのまま利用できます。
    virtual void SynchronizeFluidOutput() {}
};

} // namespace Raven::ph
