#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "Raven/Math/MathVector.h"

namespace Raven
{

// ============================================================================
// MeshVertex
// ============================================================================
// Rendererへ渡す1頂点分の論理データです。
// 現在のShader入力(position / color / texcoord)をそのまま型として表現します。
// 将来Normal/TangentやSkinning用のBoneIndex/Weightを追加するときも、Primitive生成側で
// float配列のstrideを手計算せず、この型を拡張するだけで済むようにします。
struct MeshVertex
{
    math::Vec3 Position{};
    math::Vec3 Color{ 1.0f, 1.0f, 1.0f };
    math::Vec2 TexCoord{};
};

// 頂点データがGPU Upload後も更新されるかを表します。
enum class GeometryUsage
{
    Static,
    Dynamic
};

// 頂点/Indexの接続関係そのものが変化するかを表します。
// SoftBodyやSkeletalは通常Fixed、破壊・切断MeshなどはDynamicを想定します。
enum class TopologyUsage
{
    Fixed,
    Dynamic
};

// ============================================================================
// MeshGeometry
// ============================================================================
// Meshの「形状データ」をGPUリソース(VertexArray)から分離して保持します。
//
// MeshGeometry : CPU側の論理的な頂点・Indexデータ
// Mesh         : MeshGeometryを描画可能なGPUリソースへ変換・所有するオブジェクト
//
// Dynamic Geometryでは、Skeletal / SoftBody / MorphなどのDeformerがSetVertices()を使って
// CPU側頂点を更新し、Mesh::SyncGeometry()でGPUへ反映する流れを想定しています。
class MeshGeometry
{
public:
    MeshGeometry() = default;

    MeshGeometry(
        std::vector<MeshVertex> vertices,
        std::vector<uint32_t> indices,
        GeometryUsage geometryUsage = GeometryUsage::Static,
        TopologyUsage topologyUsage = TopologyUsage::Fixed)
        : m_Vertices(std::move(vertices)),
          m_Indices(std::move(indices)),
          m_GeometryUsage(geometryUsage),
          m_TopologyUsage(topologyUsage)
    {
    }

    const std::vector<MeshVertex>& GetVertices() const { return m_Vertices; }
    const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

    GeometryUsage GetGeometryUsage() const { return m_GeometryUsage; }
    TopologyUsage GetTopologyUsage() const { return m_TopologyUsage; }

    uint64_t GetRevision() const { return m_Revision; }

    // ========================================================================
    // Dynamic vertex update
    // ========================================================================
    // Static Geometryは生成後に不変という契約なので更新を拒否します。
    // またFixed TopologyではIndex接続を維持したまま頂点属性だけを変えるため、
    // 頂点数の変更も禁止します。これにより既存Indexが範囲外を参照する事故を防ぎます。
    //
    // Dynamic Topologyで頂点数やIndexそのものを変えるAPIは、破壊/切断Meshを実装する段階で
    // 別経路として追加します。現段階では「頂点変形」と「Topology変更」を混ぜません。
    bool SetVertices(std::vector<MeshVertex> vertices)
    {
        if (m_GeometryUsage != GeometryUsage::Dynamic)
        {
            return false;
        }

        if (m_TopologyUsage == TopologyUsage::Fixed
            && vertices.size() != m_Vertices.size())
        {
            return false;
        }

        m_Vertices = std::move(vertices);
        ++m_Revision;
        return true;
    }

private:
    std::vector<MeshVertex> m_Vertices;
    std::vector<uint32_t> m_Indices;

    GeometryUsage m_GeometryUsage = GeometryUsage::Static;
    TopologyUsage m_TopologyUsage = TopologyUsage::Fixed;

    // CPU Geometryが何回変更されたかを表す単調増加カウンタです。
    // Mesh側は最後にGPUへUploadしたRevisionと比較し、不要な再Uploadを避けます。
    uint64_t m_Revision = 0;
};

} // namespace Raven
