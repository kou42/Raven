#pragma once

#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven
{

class RHIBuffer;
class RHIDevice;
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
    // MeshGeometry::SetVertices()で更新されたCPU頂点をLegacy VBOと、構築済みであれば
    // Explicit RHI用Bufferへ反映します。各ResourceのRevisionが変化していなければ、
    // 毎フレーム呼び出しても不要なUploadは発生しません。
    bool SyncGeometry();

    // 任意のRHIDeviceからExplicit Scene描画用Bufferを構築します。
    // Vulkan固有ContextはDevice実装側へ閉じ込め、Meshは共通RHIBufferだけを保持します。
    bool BuildRHIResources(RHIDevice& device);

    // Dynamic Fixed Topologyの頂点変更をExplicit RHI用Bufferへ同期します。
    // 更新に失敗した場合はRevisionを進めず、次回呼び出しで再試行できる状態を維持します。
    bool SyncRHIResources();

    const Ref<RHIBuffer>& GetRHIVertexBuffer() const
    {
        return m_RHIVertexBuffer;
    }

    const Ref<RHIBuffer>& GetRHIIndexBuffer() const
    {
        return m_RHIIndexBuffer;
    }

    bool AreRHIResourcesSynchronized() const
    {
        return m_Geometry != nullptr &&
            m_RHIVertexBuffer != nullptr &&
            m_RHIIndexBuffer != nullptr &&
            m_RHIUploadedGeometryRevision == m_Geometry->GetRevision();
    }

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

    // rebuildUploadDataがfalseの場合、同じSyncGeometry内でLegacy更新に使用した
    // 変換結果を再利用します。
    bool UploadRHIVertexData(bool rebuildUploadData);

private:
    Ref<MeshGeometry> m_Geometry;
    Ref<VertexArray> m_VertexArray;

    // Dynamic更新ではVertexArrayを作り直さず、このVBOだけをSetData()で更新します。
    Ref<VertexBuffer> m_VertexBuffer;

    // Explicit Scene RHIはLegacy VertexArrayとは独立したGPU ResourceとRevisionを持ちます。
    Ref<RHIBuffer> m_RHIVertexBuffer;
    Ref<RHIBuffer> m_RHIIndexBuffer;

    uint32_t m_IndexCount = 0;
    uint64_t m_UploadedGeometryRevision = 0;
    uint64_t m_RHIUploadedGeometryRevision = 0;

    // Dynamic Geometryの毎frame uploadでcapacityを再利用する変換先です。
    // MeshVertexの論理LayoutとGPUの11-float Layoutを分離したままHeap allocationを避けます。
    std::vector<float> m_VertexUploadData;
};

} // namespace Raven
