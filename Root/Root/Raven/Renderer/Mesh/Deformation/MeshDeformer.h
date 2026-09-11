#pragma once

#include "Raven/Physics/SoftBody/SoftBodySimulationParticipant.h"

namespace Raven
{

class Mesh;

namespace ph
{
class SoftBodySolver;
}

// ============================================================================
// MeshDeformer
// ============================================================================
// Meshの頂点変形処理を抽象化する共通インターフェースです。
//
// SoftBody Simulation Participantも基底として持ちますが、通常Deformerは既定no-opのままです。
// HasSeparatedSoftBodyUpdate()==trueの実装だけをMeshDeformationSystemがPhysics Registryへ登録します。
class MeshDeformer : public ph::SoftBodySimulationParticipant
{
public:
    virtual ~MeshDeformer() = default;

    virtual void Update(Mesh& mesh, float deltaTime) = 0;

    virtual ph::SoftBodySolver* GetSoftBodySolver() { return nullptr; }

    virtual bool HasSeparatedSoftBodyUpdate() const { return false; }

    // Clothのように初回だけMesh GeometryからPhysics Stateを構築するDeformer向けです。
    virtual bool PrepareSoftBodySimulation(Mesh& mesh)
    {
        static_cast<void>(mesh);
        return true;
    }

    // SoftBody実装だけがoverrideします。通常DeformerはRegistryへ参加しないため呼ばれません。
    void SimulateSoftBody(float deltaTime) override { static_cast<void>(deltaTime); }

    virtual void SynchronizeSoftBodyMesh(Mesh& mesh) { static_cast<void>(mesh); }
};

} // namespace Raven
