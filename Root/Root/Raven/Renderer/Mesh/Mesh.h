#pragma once

#include "Raven/Core/Base.h"

namespace Raven
{

class MeshGeometry;
class VertexArray;

// ============================================================================
// Mesh
// ============================================================================
// MeshGeometry(CPU側の論理形状)とVertexArray(GPU側の描画リソース)を結び付けます。
//
// Scene / Renderer側から見たMesh APIは従来どおり維持しつつ、形状データを独立させることで
// 将来のSkeletal / SoftBody / Morph変形をMeshGeometry側へ接続できる土台にします。
class Mesh
{
public:
    explicit Mesh(Ref<MeshGeometry> geometry);

    // PhysicsDebugRendererのように、そのフレームだけ使うGPUデータを直接組み立てる
    // 低レベル描画経路との互換用です。通常のScene MeshはMeshGeometry経由を使用します。
    // Debug描画も後でDynamic MeshGeometryへ統合できますが、今回の責務分離とは切り離して
    // 既存のデバッグ機能を壊さず段階的に移行します。
    Mesh(Ref<VertexArray> vertexArray, int32_t indexCount);

    void Draw() const;

    const Ref<MeshGeometry>& GetGeometry() const
    {
        return m_Geometry;
    }

    const Ref<VertexArray>& GetVertexArray() const
    {
        return m_VertexArray;
    }

    uint32_t GetIndexCount() const
    {
        return m_IndexCount;
    }

private:
    // MeshGeometryから現在のRenderer用GPUリソースを構築します。
    // 将来Dynamic Geometryを実装するときは、再構築ではなくVertexBuffer::SetDataを使う
    // 更新経路をここから分離して追加します。
    void BuildRenderResources();

private:
    Ref<MeshGeometry> m_Geometry;
    Ref<VertexArray> m_VertexArray;
    uint32_t m_IndexCount = 0;
};

} // namespace Raven
