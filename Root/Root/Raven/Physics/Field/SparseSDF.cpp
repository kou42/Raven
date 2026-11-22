#include "Raven/Physics/Field/SparseSDF.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace Raven::ph
{
namespace
{
using math::Vec3;
using Snapshot = SparseSDFSnapshot;
constexpr auto Width = Snapshot::TileWidth;
using Word = std::array<std::uint32_t, 4>;

bool SameGrid(const DenseSDFGrid& a, const DenseSDFGrid& b)
{
    return a.Size == b.Size && a.Origin == b.Origin && a.Spacing == b.Spacing;
}

Word Pack(Vec3 value, std::uint32_t w)
{
    return {std::bit_cast<std::uint32_t>(value.x), std::bit_cast<std::uint32_t>(value.y),
        std::bit_cast<std::uint32_t>(value.z), w};
}

// サンプル中心の座標へ変換してからタイル範囲を求めます。丸めは外側へ寄せ、候補漏れを避けます。
bool ClipBox(const DenseSDFGrid& grid, Vec3 low, Vec3 high,
    RHIComputeExtent& first, RHIComputeExtent& last)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        const double a = (static_cast<double>(low[axis]) - grid.Origin[axis]) / grid.Spacing[axis] - 0.5;
        const double b = (static_cast<double>(high[axis]) - grid.Origin[axis]) / grid.Spacing[axis] - 0.5;
        const double finalCell = static_cast<double>(grid.Size[axis] - 1);
        if (b < 0.0 || a > finalCell)
        {
            return false;
        }
        first[axis] = static_cast<std::uint32_t>(std::clamp(std::floor(a), 0.0, finalCell)) / Width;
        last[axis] = static_cast<std::uint32_t>(std::clamp(std::ceil(b), 0.0, finalCell)) / Width;
    }
    return true;
}
} // namespace

std::uint64_t SparseSDFSnapshot::GetGeneration() const { return m_Generation; }
std::size_t SparseSDFSnapshot::GetTileCount() const { return m_Tiles.size(); }
bool SparseSDFSnapshot::IsSigned() const { return m_Signed; }
const DenseSDFGrid& SparseSDFSnapshot::GetGrid() const { return m_Grid; }
std::size_t SparseSDFSnapshot::GetPayloadBytes() const
{
    return m_Tiles.size() * (sizeof(Tile) + sizeof(RHIComputeExtent));
}

bool SparseSDFSnapshot::TryGetSupportCell(const RHIComputeExtent& cell, float& distance) const
{
    for (std::size_t axis = 0; axis < cell.size(); ++axis)
    {
        if (cell[axis] >= m_Grid.Size[axis])
        {
            return false;
        }
    }
    const auto tile = m_Tiles.find({cell[0] / Width, cell[1] / Width, cell[2] / Width});
    if (tile == m_Tiles.end())
    {
        return false;
    }
    const float value = (*tile->second)[cell[0] % Width + Width * (cell[1] % Width + Width * (cell[2] % Width))];
    if (std::isfinite(value) == false || std::abs(value) > m_Support)
    {
        return false;
    }
    distance = value;
    return true;
}

bool SparseSDFSnapshot::TryGetCell(const RHIComputeExtent& cell, float& distance) const
{
    float value = 0.0f;
    if (TryGetSupportCell(cell, value) == false || std::abs(value) > m_Band)
    {
        return false;
    }
    distance = value;
    return true;
}

bool SparseSDFSnapshot::TrySample(Vec3 p, float& distance) const
{
    return Sample(p, distance) == SparseSDFSampleStatus::Valid;
}

SparseSDFSampleStatus SparseSDFSnapshot::Sample(Vec3 p, float& distance) const
{
    if (m_Grid.GetVoxelCount() == 0)
    {
        return SparseSDFSampleStatus::OutsideGrid;
    }
    RHIComputeExtent lo{}, hi{};
    Vec3 fraction{};
    const auto first = m_Grid.GetCellCenter(0, 0, 0);
    const auto last = m_Grid.GetCellCenter(m_Grid.Size[0] - 1, m_Grid.Size[1] - 1, m_Grid.Size[2] - 1);
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::isfinite(p[axis]) == false || p[axis] < first[axis] || p[axis] > last[axis])
        {
            return SparseSDFSampleStatus::OutsideGrid;
        }
        // world-space側で領域を判定済みです。逆変換の端で生じる丸めだけを吸収します。
        const double coordinate = std::clamp(
            (static_cast<double>(p[axis]) - m_Grid.Origin[axis]) / m_Grid.Spacing[axis] - 0.5,
            0.0, static_cast<double>(m_Grid.Size[axis] - 1));
        lo[axis] = static_cast<std::uint32_t>(coordinate);
        hi[axis] = std::min(lo[axis] + 1u, m_Grid.Size[axis] - 1);
        fraction[axis] = static_cast<float>(coordinate - lo[axis]);
    }
    float value = 0.0f;
    for (auto z : {0, 1})
    {
        for (auto y : {0, 1})
        {
            for (auto x : {0, 1})
            {
                const float weight = (x ? fraction.x : 1.0f - fraction.x) *
                    (y ? fraction.y : 1.0f - fraction.y) * (z ? fraction.z : 1.0f - fraction.z);
                if (weight == 0.0f)
                {
                    continue;
                }
                float sample = 0.0f;
                if (TryGetSupportCell({x ? hi[0] : lo[0], y ? hi[1] : lo[1], z ? hi[2] : lo[2]}, sample) == false)
                {
                    return SparseSDFSampleStatus::MissingSupport;
                }
                value += sample * weight;
            }
        }
    }
    if (std::abs(value) > m_Band)
    {
        return SparseSDFSampleStatus::OutsideBand;
    }
    distance = value;
    return SparseSDFSampleStatus::Valid;
}

SparseSDFUpdater::Candidates SparseSDFUpdater::BuildCandidates(
    const DenseSDFSource& source, const DenseSDFGrid& grid, float support)
{
    Candidates result;
    const auto addBox = [&](Vec3 low, Vec3 high, std::optional<std::uint32_t> triangle)
    {
        RHIComputeExtent first{}, last{};
        if (ClipBox(grid, low, high, first, last) == false)
        {
            return;
        }
        for (auto z = first[2]; z <= last[2]; ++z)
        {
            for (auto y = first[1]; y <= last[1]; ++y)
            {
                for (auto x = first[0]; x <= last[0]; ++x)
                {
                    const RHIComputeExtent key{x, y, z};
                    if (triangle.has_value())
                    {
                        result[key].push_back(*triangle);
                        continue;
                    }
                    // 球の内側深くにあるタイルは、タイル内の最遠点でも帯域に届きません。
                    const Vec3 a = grid.GetCellCenter(x * Width, y * Width, z * Width) - source.GetCenter();
                    const Vec3 b = grid.GetCellCenter(std::min(x * Width + Width - 1, grid.Size[0] - 1),
                        std::min(y * Width + Width - 1, grid.Size[1] - 1),
                        std::min(z * Width + Width - 1, grid.Size[2] - 1)) - source.GetCenter();
                    const Vec3 near{std::max({a.x, -b.x, 0.0f}), std::max({a.y, -b.y, 0.0f}), std::max({a.z, -b.z, 0.0f})};
                    const Vec3 far{std::max(std::abs(a.x), std::abs(b.x)), std::max(std::abs(a.y), std::abs(b.y)),
                        std::max(std::abs(a.z), std::abs(b.z))};
                    if (near.Length() <= source.GetRadius() + support && far.Length() >= source.GetRadius() - support)
                    {
                        result.try_emplace(key);
                    }
                }
            }
        }
    };
    if (source.GetMode() == DenseSDFSource::Mode::Sphere)
    {
        const Vec3 radius(source.GetRadius() + support);
        addBox(source.GetCenter() - radius, source.GetCenter() + radius, std::nullopt);
        return result;
    }
    const auto vertices = source.GetTriangleVertices();
    for (std::size_t i = 0; i < vertices.size(); i += 3)
    {
        const auto a = vertices[i];
        const auto b = vertices[i + 1];
        const auto c = vertices[i + 2];
        const Vec3 low{std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::min({a.z, b.z, c.z})};
        const Vec3 high{std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}), std::max({a.z, b.z, c.z})};
        addBox(low - Vec3(support), high + Vec3(support), static_cast<std::uint32_t>(i / 3));
    }
    return result;
}

bool SparseSDFUpdater::BeginUpdate(const DenseSDFSource& source, const DenseSDFGrid& grid, float band)
{
    // 補間の8サンプルを残すため、公開する帯域にセル対角長の余白を足します。
    const float support = band + grid.Spacing.Length();
    if (m_Staged != nullptr || grid.GetVoxelCount() == 0 || band <= 0.0f || std::isfinite(support) == false ||
        (m_Snapshot != nullptr && m_Snapshot->m_Generation == std::numeric_limits<std::uint64_t>::max()))
    {
        return false;
    }
    auto next = CreateRef<SparseSDFSnapshot>();
    next->m_Grid = grid;
    next->m_Band = band;
    next->m_Signed = source.GetMode() != DenseSDFSource::Mode::UnsignedMesh;
    next->m_Support = support;
    next->m_Generation = m_Snapshot == nullptr ? 1 : m_Snapshot->m_Generation + 1;
    const bool compatible = m_Snapshot != nullptr && SameGrid(grid, m_Snapshot->m_Grid) &&
        band == m_Snapshot->m_Band && source.GetMode() == m_Source->GetMode();
    const auto vertices = source.GetTriangleVertices();
    const auto oldVertices = compatible ? m_Source->GetTriangleVertices() : std::span<const Vec3>{};
    const bool identical = compatible && source.GetCenter() == m_Source->GetCenter() &&
        source.GetRadius() == m_Source->GetRadius() &&
        std::equal(vertices.begin(), vertices.end(), oldVertices.begin(), oldVertices.end());
    if (identical)
    {
        // タイル本体と候補の空間分類は不変。世代だけ進め、候補再走査と GPU 更新を省く。
        next->m_Tiles = m_Snapshot->m_Tiles;
        m_Source = source;
        m_Snapshot = std::move(next);
        return true;
    }
    m_NextSource = source;
    m_NextCandidates = BuildCandidates(source, grid, support);
    const auto topology = source.GetTriangleIndices();
    const auto oldTopology = compatible ? m_Source->GetTriangleIndices() : std::span<const DenseSDFSource::Triangle>{};
    const bool signedEdit = compatible && source.GetMode() == DenseSDFSource::Mode::ClosedMesh &&
        std::equal(topology.begin(), topology.end(), oldTopology.begin(), oldTopology.end());
    struct TileRange { RHIComputeExtent First; RHIComputeExtent Last; };
    std::vector<TileRange> sweptTiles;
    if (signedEdit)
    {
        for (std::size_t i = 0; i < vertices.size(); i += 3)
        {
            if (std::equal(vertices.begin() + i, vertices.begin() + i + 3, oldVertices.begin() + i))
            {
                continue;
            }
            auto low = vertices[i];
            auto high = low;
            for (const auto geometry : {vertices, oldVertices})
            {
                for (std::size_t vertex = i; vertex < i + 3; ++vertex)
                {
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        low[axis] = std::min(low[axis], geometry[vertex][axis]);
                        high[axis] = std::max(high[axis], geometry[vertex][axis]);
                    }
                }
            }
            TileRange range{};
            // 同じ接続の頂点を線形に移動すると、面は始点・終点のAABB内しか通過しません。
            // 移動した面から離れた点の巻き数は維持されるので、帯域分の余白の外は再利用できます。
            if (ClipBox(grid, low - Vec3(support), high + Vec3(support), range.First, range.Last))
            {
                sweptTiles.push_back(range);
            }
        }
    }
    for (const auto& [tile, triangles] : m_NextCandidates)
    {
        bool reuse = false;
        if (signedEdit)
        {
            const auto old = m_Candidates.find(tile);
            reuse = old != m_Candidates.end() && old->second == triangles &&
                std::none_of(sweptTiles.begin(), sweptTiles.end(), [&](const TileRange& range)
                {
                    return tile[0] >= range.First[0] && tile[0] <= range.Last[0] &&
                        tile[1] >= range.First[1] && tile[1] <= range.Last[1] &&
                        tile[2] >= range.First[2] && tile[2] <= range.Last[2];
                });
        }
        if (compatible && source.GetMode() == DenseSDFSource::Mode::UnsignedMesh)
        {
            const auto old = m_Candidates.find(tile);
            reuse = old != m_Candidates.end() && old->second == triangles;
            for (const auto triangle : triangles)
            {
                const auto index = static_cast<std::size_t>(triangle) * 3;
                reuse = reuse && index + 2 < oldVertices.size() &&
                    std::equal(vertices.begin() + index, vertices.begin() + index + 3, oldVertices.begin() + index);
            }
        }
        // 接続が変わった閉じたMeshは、遠方も含めてすべての現行タイルを再生成します。
        if (reuse)
        {
            next->m_Tiles.emplace(tile, m_Snapshot->m_Tiles.at(tile));
        }
        else
        {
            m_Dirty.push_back(tile);
        }
    }
    m_Staged = std::move(next);
    if (m_Dirty.empty())
    {
        Publish();
    }
    return true;
}

bool SparseSDFUpdater::PrepareBatch(RHIComputeCommandEncoder& encoder,
    RHIComputePipelineSpecification shader, std::uint32_t maxTiles)
{
    if (m_Staged == nullptr || m_BatchTiles != 0 || maxTiles == 0)
    {
        return false;
    }
    // 1 workgroupあたり1タイル。3 Backendで扱えるDispatch上限を越えないようにします。
    const auto count = static_cast<std::uint32_t>(std::min({m_Dirty.size() - m_NextTile,
        static_cast<std::size_t>(maxTiles), std::size_t{65535}}));
    const auto& grid = m_Staged->m_Grid;
    const auto& source = *m_NextSource;
    const auto vertices = source.GetTriangleVertices();
    constexpr auto maxWords = std::numeric_limits<std::uint32_t>::max() / sizeof(Word);
    constexpr std::size_t header = 5;
    if (vertices.size() > maxWords - header - 2u * count)
    {
        return false;
    }
    std::vector<Word> input;
    input.push_back(Pack(grid.Origin, static_cast<std::uint32_t>(source.GetMode())));
    input.push_back(Pack(grid.Spacing, static_cast<std::uint32_t>(vertices.size() / 3)));
    input.push_back({grid.Size[0], grid.Size[1], grid.Size[2], count});
    input.push_back(Pack(source.GetCenter(), std::bit_cast<std::uint32_t>(source.GetRadius())));
    input.push_back({std::bit_cast<std::uint32_t>(m_Staged->m_Support), 0, 0, 0});
    for (const auto& vertex : vertices)
    {
        input.push_back(Pack(vertex, 0));
    }
    const auto tileStart = input.size();
    input.resize(input.size() + 2u * count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto& tile = m_Dirty[m_NextTile + i];
        const auto& triangles = m_NextCandidates.at(tile);
        if (triangles.size() > maxWords - input.size())
        {
            return false;
        }
        input[tileStart + 2u * i] = {tile[0] * Width, tile[1] * Width, tile[2] * Width,
            static_cast<std::uint32_t>(input.size())};
        input[tileStart + 2u * i + 1u][0] = static_cast<std::uint32_t>(triangles.size());
        for (const auto triangle : triangles)
        {
            input.push_back({triangle, 0, 0, 0});
        }
    }
    shader.LocalSize = {Width, Width, Width};
    shader.Layout = {{0, RHIComputeBindingType::StorageBuffer, RHIComputeAccess::Read},
        {1, RHIComputeBindingType::StorageBuffer, RHIComputeAccess::Write}};
    const auto identity = encoder.GetIdentity().lock();
    const bool sameEncoder = m_EncoderIdentity.lock() == identity;
    if (m_NextTile != 0 && (sameEncoder == false || m_Shader != shader))
    {
        // 1 世代のタイルは同じ Backend と Shader の結果だけで構成する。
        return false;
    }
    auto pipeline = m_Pipeline != nullptr && sameEncoder && m_Shader == shader
        ? m_Pipeline : encoder.CreatePipeline(shader);
    auto parameters = encoder.CreateStorageBuffer(input.size() * sizeof(Word), input.data());
    const auto bytes = count * sizeof(Snapshot::Tile);
    const bool reuseOutput = sameEncoder && m_Output != nullptr && bytes <= m_OutputCapacity;
    // 前 Batch の Readback が Ready を返してから同じ Buffer を上書きする。
    auto output = reuseOutput ? m_Output : encoder.CreateStorageBuffer(bytes, nullptr);
    if (pipeline == nullptr || parameters == nullptr || output == nullptr)
    {
        return false;
    }
    m_Pipeline = std::move(pipeline);
    m_EncoderIdentity = identity;
    m_Shader = std::move(shader);
    m_Input = std::move(parameters);
    m_Output = std::move(output);
    m_OutputCapacity = reuseOutput ? m_OutputCapacity : bytes;
    m_BatchTiles = count;
    m_BatchBytes = input.size() * sizeof(Word) + m_OutputCapacity;
    return true;
}

bool SparseSDFUpdater::RecordBatch(RHIComputeCommandEncoder& encoder)
{
    if (m_BatchTiles == 0 || m_Readback != nullptr || m_EncoderIdentity.lock() != encoder.GetIdentity().lock())
    {
        return false;
    }
    const RHIComputeBinding bindings[] = {{0, m_Input}, {1, m_Output}};
    if (encoder.Dispatch(m_Pipeline, bindings, {m_BatchTiles, 1, 1}) == false ||
        encoder.Barrier(RHIComputeConsumer::BufferReadback) == false)
    {
        return false;
    }
    m_Readback = encoder.RequestBufferReadback(m_Output, 0, m_BatchTiles * sizeof(Snapshot::Tile));
    return m_Readback != nullptr;
}

SparseSDFUpdateStatus SparseSDFUpdater::Poll()
{
    if (m_Readback == nullptr)
    {
        return m_Staged == nullptr ? SparseSDFUpdateStatus::Idle : SparseSDFUpdateStatus::Pending;
    }
    std::vector<std::byte> bytes;
    const auto state = m_Readback->Poll(bytes);
    if (state == RHIComputeReadbackStatus::Pending)
    {
        return SparseSDFUpdateStatus::Pending;
    }
    if (state != RHIComputeReadbackStatus::Ready || bytes.size() != m_BatchTiles * sizeof(Snapshot::Tile))
    {
        Cancel();
        return SparseSDFUpdateStatus::Failed;
    }
    for (std::uint32_t i = 0; i < m_BatchTiles; ++i)
    {
        auto samples = CreateRef<Snapshot::Tile>();
        std::memcpy(samples->data(), bytes.data() + i * sizeof(Snapshot::Tile), sizeof(Snapshot::Tile));
        m_Staged->m_Tiles.emplace(m_Dirty[m_NextTile + i], std::move(samples));
    }
    m_NextTile += m_BatchTiles;
    ResetBatch();
    if (m_NextTile == m_Dirty.size())
    {
        Publish();
        return SparseSDFUpdateStatus::Published;
    }
    return SparseSDFUpdateStatus::BatchComplete;
}

void SparseSDFUpdater::ResetBatch()
{
    m_Readback.reset();
    m_Input.reset();
    m_BatchTiles = 0;
    m_BatchBytes = 0;
}

void SparseSDFUpdater::Publish()
{
    m_Snapshot = std::move(m_Staged);
    m_Source = std::move(m_NextSource);
    m_Candidates = std::move(m_NextCandidates);
    ResetUpdate();
}

void SparseSDFUpdater::Cancel()
{
    ResetUpdate();
    m_Pipeline.reset();
    m_EncoderIdentity.reset();
    m_Shader = {};
}

void SparseSDFUpdater::ResetUpdate()
{
    ResetBatch();
    m_Output.reset();
    m_OutputCapacity = 0;
    m_Staged.reset();
    m_NextSource.reset();
    m_NextCandidates.clear();
    m_Dirty.clear();
    m_NextTile = 0;
}

Ref<const SparseSDFSnapshot> SparseSDFUpdater::GetSnapshot() const { return m_Snapshot; }
std::size_t SparseSDFUpdater::GetRemainingTileCount() const { return m_Dirty.size() - m_NextTile; }
std::size_t SparseSDFUpdater::GetPreparedPayloadBytes() const { return m_BatchBytes; }

} // namespace Raven::ph
