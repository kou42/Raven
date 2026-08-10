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
// 現段階では分類のみを導入し、Dynamic用の部分更新APIは後続実装で追加します。
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
// この分離により、将来Skeletal / SoftBody / Morphなどが頂点を変形するときに
// Renderer固有のVertexArrayを直接操作せず、Geometryを中心に機能を追加できます。
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

private:
    std::vector<MeshVertex> m_Vertices;
    std::vector<uint32_t> m_Indices;

    GeometryUsage m_GeometryUsage = GeometryUsage::Static;
    TopologyUsage m_TopologyUsage = TopologyUsage::Fixed;
};

} // namespace Raven
