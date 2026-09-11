#pragma once

namespace Raven::ph
{

// ============================================================================
// SoftBodySimulationParticipant
// ============================================================================
// SoftBodyの所有場所(Renderer Deformer等)をPhysics Domainから隠したまま、
// Fixed StepでSimulationだけを進めるための最小インターフェースです。
//
// Physics側はMeshや具体的なCloth/Jelly型へ依存せず、この境界だけを非所有参照として保持します。
// Mesh初期化と描画同期はParticipantの責務に含めず、Physics State更新だけに限定します。
class SoftBodySimulationParticipant
{
public:
    virtual ~SoftBodySimulationParticipant() = default;
    virtual void SimulateSoftBody(float fixedDeltaTime) = 0;
};

} // namespace Raven::ph
