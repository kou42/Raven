#pragma once

namespace Raven
{

class Mesh;

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
// Skeletal / SoftBody / Morphは将来このインターフェースを実装し、Renderer固有の
// VertexArray / VertexBufferを直接操作しない構造を維持します。
class MeshDeformer
{
public:
    virtual ~MeshDeformer() = default;

    // deltaTime秒だけ変形状態を進め、必要ならMeshGeometryを更新します。
    // GPU同期までDeformer側で完結させることで、Scene側はUpdate()を呼ぶだけで済みます。
    virtual void Update(Mesh& mesh, float deltaTime) = 0;
};

} // namespace Raven
