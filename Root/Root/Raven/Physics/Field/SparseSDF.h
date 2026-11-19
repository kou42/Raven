#pragma once

#include "Raven/Physics/Field/DenseSDF.h"

#include <map>

namespace Raven::ph
{

// 帯域外や必要なタイルの欠損は照会失敗として返します。距離0で代用しません。
class SparseSDFSnapshot
{
public:
    static constexpr std::uint32_t TileWidth = 4;
    static constexpr std::size_t SamplesPerTile = TileWidth * TileWidth * TileWidth;
    using Tile = std::array<float, SamplesPerTile>;

    std::uint64_t GetGeneration() const;
    std::size_t GetTileCount() const;
    std::size_t GetPayloadBytes() const;
    bool TryGetCell(const RHIComputeExtent& cell, float& distance) const;
    bool TrySample(math::Vec3 worldPosition, float& distance) const;

private:
    friend class SparseSDFUpdater;
    bool TryGetSupportCell(const RHIComputeExtent& cell, float& distance) const;
    DenseSDFGrid m_Grid;
    float m_Band = 0.0f;
    float m_Support = 0.0f;
    std::uint64_t m_Generation = 0;
    std::map<RHIComputeExtent, Ref<const Tile>> m_Tiles;
};

enum class SparseSDFUpdateStatus { Idle, Pending, BatchComplete, Published, Failed };

// 1回に1バッチだけをGPUに送ります。全バッチの完了まで前のSnapshotを公開します。
class SparseSDFUpdater
{
public:
    bool BeginUpdate(const DenseSDFSource& source, const DenseSDFGrid& grid, float band);
    bool PrepareBatch(RHIComputeCommandEncoder& encoder, RHIComputePipelineSpecification shader,
        std::uint32_t maxTiles);
    bool RecordBatch(RHIComputeCommandEncoder& encoder);
    SparseSDFUpdateStatus Poll();
    void Cancel();
    Ref<const SparseSDFSnapshot> GetSnapshot() const;
    std::size_t GetRemainingTileCount() const;
    std::size_t GetPreparedPayloadBytes() const;

private:
    using Candidates = std::map<RHIComputeExtent, std::vector<std::uint32_t>>;
    static Candidates BuildCandidates(const DenseSDFSource& source, const DenseSDFGrid& grid, float support);
    void Publish();
    void ResetBatch();

    Ref<const SparseSDFSnapshot> m_Snapshot;
    std::optional<DenseSDFSource> m_Source;
    Candidates m_Candidates;

    Ref<SparseSDFSnapshot> m_Staged;
    std::optional<DenseSDFSource> m_NextSource;
    Candidates m_NextCandidates;
    std::vector<RHIComputeExtent> m_Dirty;
    std::size_t m_NextTile = 0;
    std::uint32_t m_BatchTiles = 0;
    std::size_t m_BatchBytes = 0;
    Ref<RHIComputePipeline> m_Pipeline;
    Ref<RHIComputeResource> m_Input;
    Ref<RHIComputeResource> m_Output;
    Ref<RHIComputeReadback> m_Readback;
};

} // namespace Raven::ph
