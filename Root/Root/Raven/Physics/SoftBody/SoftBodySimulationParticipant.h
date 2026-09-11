#pragma once

namespace Raven::ph
{

// ============================================================================
// SoftBodySimulationParticipant
// ============================================================================
// SoftBodyの所有場所(Renderer Deformer等)をPhysics Domainから隠したまま、
// Fixed StepでSimulationを進め、その直後に出力同期を要求するための最小境界です。
//
// Physics側はMeshや具体的なCloth/Jelly型へ依存しません。
// SynchronizeSoftBodyOutput()の具体処理もParticipant側へ閉じるため、Physics Domainから
// Renderer型を参照せずに「最新Physics Stateを外部表現へ反映する」順序だけを保証できます。
class SoftBodySimulationParticipant
{
public:
    virtual ~SoftBodySimulationParticipant() = default;

    virtual void SimulateSoftBody(float fixedDeltaTime) = 0;

    // Simulation参加者が外部出力を持つ場合だけoverrideします。
    // 純粋なPhysics Test Participantは既定no-opのまま利用できます。
    virtual void SynchronizeSoftBodyOutput() {}
};

} // namespace Raven::ph
