#pragma once

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
class MeshDeformer
{
public:
    virtual ~MeshDeformer() = default;

    virtual void Update(Mesh& mesh, float deltaTime) = 0;

    // SoftBody DeformerだけがSolver参照を公開する任意インターフェースです。
    virtual ph::SoftBodySolver* GetSoftBodySolver() { return nullptr; }

    // SoftBody SimulationとMesh同期をFixed Stepへ段階移管するための任意境界です。
    // falseのDeformerは従来どおりUpdate()が全責務を持つため、既存実装への影響はありません。
    // trueの実装ではPhysics State更新と描画用Mesh同期を別々に呼び出せます。
    virtual bool HasSeparatedSoftBodyUpdate() const { return false; }
    virtual void SimulateSoftBody(float deltaTime) { static_cast<void>(deltaTime); }
    virtual void SynchronizeSoftBodyMesh(Mesh& mesh) { static_cast<void>(mesh); }
};

} // namespace Raven
