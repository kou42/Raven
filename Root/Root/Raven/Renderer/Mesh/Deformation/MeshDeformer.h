#pragma once

#include <cstdint>

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
// Meshの頂点変形処理だけを抽象化する共通インターフェースです。
//
// 重要:
// - MeshGeometry      : CPU側の論理頂点を保持する
// - MeshDeformer      : CPU頂点をどう変形するかを決める
// - Mesh::SyncGeometry: 変形結果をGPUへ同期する
//
// という3段階に責務を分離します。
// Skeletal / SoftBody / Morphはこのインターフェースを実装し、Renderer固有の
// VertexArray / VertexBufferを直接操作しない構造を維持します。
//
// SoftBody Simulation Participantも基底として持ちますが、通常Deformerは既定no-opのままです。
// HasSeparatedSoftBodyUpdate()==trueの実装だけをMeshDeformationSystemがPhysics Registryへ登録します。
class MeshDeformer : public ph::SoftBodySimulationParticipant
{
public:
    virtual ~MeshDeformer() = default;

    // deltaTime秒だけ変形状態を進め、必要ならMeshGeometryを更新します。
    // 非SoftBody Deformerでは従来どおりGPU同期までDeformer側で完結し、Scene側はUpdate()を呼ぶだけです。
    // SoftBodyはFixed Step移管のため、下記のPrepare / Simulate / Synchronize境界へ責務を分離できます。
    virtual void Update(Mesh& mesh, float deltaTime) = 0;

    // SoftBody DeformerだけがSolver参照を公開する任意インターフェースです。
    // 所有権はDeformer側に残し、SoftBodyWorldではDebug/Coupling用の非所有参照として扱います。
    virtual ph::SoftBodySolver* GetSoftBodySolver() { return nullptr; }

    // Rigid/Soft Sphere Couplingへ参加できるDeformerだけが同期先Collider Indexを公開します。
    // MeshDeformationSystemは具体的なCloth型へdowncastせず、この共通境界からRuntime Bindingを構築します。
    virtual bool TryGetSoftBodySphereColliderIndex(uint32_t& outColliderIndex) const
    {
        static_cast<void>(outColliderIndex);
        return false;
    }

    // falseのDeformerは従来どおりUpdate()が全責務を持ちます。
    // trueの実装だけがPhysics Fixed StepのSimulation Participantとして登録されます。
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

    // Physics側はRendererのMesh型を知らないため、Game/Renderer側で事前に同期対象だけを関連付けます。
    // MeshDeformationInstanceがMeshとDeformerを同じlifetimeで所有し、Destroy QueueはPhysics後に処理されるため、
    // 登録frame中はこの非所有pointerを安全に使用できます。
    void BindSoftBodySynchronizationMesh(Mesh& mesh)
    {
        m_SoftBodySynchronizationMesh = &mesh;
    }

    void SynchronizeSoftBodyOutput() override
    {
        if (m_SoftBodySynchronizationMesh == nullptr)
        {
            return;
        }

        SynchronizeSoftBodyMesh(*m_SoftBodySynchronizationMesh);
    }

private:
    Mesh* m_SoftBodySynchronizationMesh = nullptr;
};

} // namespace Raven
