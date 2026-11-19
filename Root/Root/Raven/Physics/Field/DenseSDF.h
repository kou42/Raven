#pragma once

#include "Raven/Physics/Field/PhysicsField.h"
#include "Raven/Renderer/RHI/RHICompute.h"

#include <optional>

namespace Raven::ph
{

// 距離はメートル、閉じた形状は内側が負です。開いたMeshの距離は符号なしです。
class DenseSDFSource final : public ScalarField
{
public:
    enum class Mode { Sphere, UnsignedMesh, ClosedMesh };
    using Triangle = std::array<std::uint32_t, 3>;

    static std::optional<DenseSDFSource> CreateSphere(math::Vec3 center, float radius);
    // 頂点はworld-spaceへ変換済みとします。閉じたMeshは一貫した面の向きが必要です。
    static std::optional<DenseSDFSource> CreateMesh(std::span<const math::Vec3> vertices,
        std::span<const Triangle> triangles, Mode mode);
    float Evaluate(const math::Vec3& worldPosition) const override;

    Mode GetMode() const;
    math::Vec3 GetCenter() const;
    float GetRadius() const;
    std::span<const math::Vec3> GetTriangleVertices() const;
    std::span<const Triangle> GetTriangleIndices() const;

private:
    DenseSDFSource() = default;
    Mode m_Mode = Mode::Sphere;
    math::Vec3 m_Center{};
    float m_Radius = 0.0f;
    std::vector<math::Vec3> m_TriangleVertices;
    std::vector<Triangle> m_TriangleIndices;
};

struct DenseSDFGrid
{
    math::Vec3 Origin{};
    math::Vec3 Spacing{};
    RHIComputeExtent Size{};

    math::Vec3 GetCellCenter(std::uint32_t x, std::uint32_t y, std::uint32_t z) const;
    std::size_t GetVoxelCount() const;
    bool TryGetNormal(std::span<const float> distances, const RHIComputeExtent& cell,
        math::Vec3& normal) const;
};

// Phase 05では同じ距離を3D ImageとReadback用Bufferへ書きます。生成と描画の同期は
// RecordのBarrierで行い、CPUからの取得はチケットがReadyになった後に行います。
class DenseSDFGenerator
{
public:
    bool Prepare(RHIComputeCommandEncoder& encoder, RHIComputePipelineSpecification shader,
        const DenseSDFSource& source, const DenseSDFGrid& grid);
    Ref<RHIComputeReadback> Record(RHIComputeCommandEncoder& encoder) const;
    bool RecordTexture(RHIComputeCommandEncoder& encoder) const;
    const Ref<RHIComputeResource>& GetTexture() const;
    const DenseSDFGrid& GetGrid() const;
    bool IsSigned() const;

private:
    Ref<RHIComputePipeline> m_Pipeline;
    Ref<RHIComputeResource> m_Input;
    Ref<RHIComputeResource> m_Distances;
    Ref<RHIComputeResource> m_Texture;
    RHIComputeExtent m_Groups{};
    std::size_t m_Bytes = 0;
    DenseSDFGrid m_Grid;
    bool m_Signed = false;
};

} // namespace Raven::ph
