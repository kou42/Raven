#pragma once

#include "Raven/Core/Base.h"

namespace Raven
{

class MeshGeometry;
class VertexArray;
class VertexBuffer;

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
    Mesh(Ref<VertexArray> vertexArray, int32_t indexCount);

    void Draw() const;

    // ========================================================================
    // Dynamic Geometry synchronization
    // ========================================================================
    // MeshGeometry::SetVertices()で更新されたCPU頂点を既存VBOへ反映します。
    // Revisionが変化していなければ何もしないため、毎フレーム呼び出しても不要なUploadは
    // 発生しません。Static Geometryや低レベルVertexArray Meshではfalseを返します。
    bool SyncGeometry();

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
    void BuildRenderResources();

    // MeshVertex配列を現在のShader入力に対応する連続float列へ変換し、VBOへ送ります。
    // 初期UploadとDynamic更新で同じ変換規約を共有するためのヘルパーです。
    bool UploadVertexData();

private:
    Ref<MeshGeometry> m_Geometry;
    Ref<VertexArray> m_VertexArray;

    // Dynamic更新ではVertexArrayを作り直さず、このVBOだけをSetData()で更新します。
    Ref<VertexBuffer> m_VertexBuffer;

    uint32_t m_IndexCount = 0;
    uint64_t m_UploadedGeometryRevision = 0;
};

} // namespace Raven
