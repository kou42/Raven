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
//
// Position / Color / TexCoord は既存Primitiveや既存描画コードとの互換性を維持するため
// 並び順を変更しません。Normalは末尾へ追加することで、既存の3要素aggregate初期化を
// そのまま利用できるようにしています。
//
// TexCoordはRavenの論理UVとして扱い、左上(0, 0)・右下(1, 1)、Vは下方向へ増加する規約です。
// glTFのTEXCOORD_0はこの規約と一致するためImporterで値を反転せず、そのまま保持します。
// OpenGL等のnative Texture座標系との差はTexture samplingを行うRenderer backend側で吸収します。
//
// Human / glTF ImporterではNORMAL attributeをNormalへ格納します。
// 将来Normal Mapへ進む段階では、この型へTangentを追加する想定です。
struct MeshVertex
{
    math::Vec3 Position{};
    math::Vec3 Color{ 1.0f, 1.0f, 1.0f };
    math::Vec2 TexCoord{};

    // 外部AssetにNORMALが存在しないケースも区別できるよう、既定値はゼロベクトルです。
    // glTF Importer実装時はNORMAL accessorから読み取った値を設定します。
    math::Vec3 Normal{};
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

    // Dynamic Fixed Topologyを毎フレーム変形する処理向けのゼロallocation更新です。
    // 呼び出し側の作業BufferとGeometryのBufferを交換するため、成功後のverticesには更新前の
    // Geometry頂点が入ります。次frameで同じBufferを再利用でき、SetVertices(move)で毎回失われる
    // vector capacityを往復利用できます。
    bool SwapVertices(std::vector<MeshVertex>& vertices)
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

        m_Vertices.swap(vertices);
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
