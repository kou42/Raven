#include "Raven/Physics/SoftBody/Debug/SoftBodyPhysicsDebugSvgWriter.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <tuple>
#include <utility>

namespace Raven
{
namespace ph
{
namespace
{
struct ProjectionBounds
{
    float MinX = 0.0f;
    float MaxX = 0.0f;
    float MinY = 0.0f;
    float MaxY = 0.0f;
    float MinZ = 0.0f;
    float MaxZ = 0.0f;
};

struct ProjectedOccupiedCell
{
    uint64_t TriangleRegistrationCount = 0u;
    uint32_t ZLayerCount = 0u;
    int32_t MinimumZ = 0;
    int32_t MaximumZ = 0;
};

struct ParticleQueryCellInfo
{
    uint32_t ParticleCount = 0u;
    uint32_t TriangleCountPerParticle = 0u;
};

struct ParticleQueryDebugData
{
    std::map<std::tuple<int32_t, int32_t, int32_t>, uint32_t> ActiveCellTriangleCounts;
    std::map<std::tuple<int32_t, int32_t, int32_t>, ParticleQueryCellInfo> HitCells;
    uint64_t HitParticleCount = 0u;
    uint64_t CellCandidateCount = 0u;
};

ProjectionBounds ComputeBounds(const std::vector<SoftBodyParticle>& particles)
{
    ProjectionBounds bounds{};
    if (particles.empty() == true)
    {
        return bounds;
    }

    bounds.MinX = particles.front().Position.x;
    bounds.MaxX = particles.front().Position.x;
    bounds.MinY = particles.front().Position.y;
    bounds.MaxY = particles.front().Position.y;
    bounds.MinZ = particles.front().Position.z;
    bounds.MaxZ = particles.front().Position.z;

    for (const SoftBodyParticle& particle : particles)
    {
        bounds.MinX = std::min(bounds.MinX, particle.Position.x);
        bounds.MaxX = std::max(bounds.MaxX, particle.Position.x);
        bounds.MinY = std::min(bounds.MinY, particle.Position.y);
        bounds.MaxY = std::max(bounds.MaxY, particle.Position.y);
        bounds.MinZ = std::min(bounds.MinZ, particle.Position.z);
        bounds.MaxZ = std::max(bounds.MaxZ, particle.Position.z);
    }

    return bounds;
}

float SafeRange(float minimum, float maximum)
{
    return std::max(maximum - minimum, 1.0e-5f);
}

float ProjectX(
    float x,
    const ProjectionBounds& bounds,
    const SoftBodyPhysicsDebugSvgWriter::Settings& settings)
{
    const float drawableWidth = std::max(
        static_cast<float>(settings.Width) - settings.Padding * 2.0f,
        1.0f);
    return settings.Padding
        + ((x - bounds.MinX) / SafeRange(bounds.MinX, bounds.MaxX)) * drawableWidth;
}

float ProjectY(
    float y,
    const ProjectionBounds& bounds,
    const SoftBodyPhysicsDebugSvgWriter::Settings& settings)
{
    const float drawableHeight = std::max(
        static_cast<float>(settings.Height) - settings.Padding * 2.0f - 150.0f,
        1.0f);

    // SVGは下向きが+Yなので、Physicsの+Yが画面上方向になるよう反転します。
    return settings.Padding + 120.0f
        + (1.0f - ((y - bounds.MinY) / SafeRange(bounds.MinY, bounds.MaxY))) * drawableHeight;
}

float NormalizeDepth(float z, const ProjectionBounds& bounds)
{
    return (z - bounds.MinZ) / SafeRange(bounds.MinZ, bounds.MaxZ);
}

std::tuple<int32_t, int32_t, int32_t> ComputeCellKey(
    const math::Vec3& position,
    float cellSize)
{
    return {
        static_cast<int32_t>(std::floor(position.x / cellSize)),
        static_cast<int32_t>(std::floor(position.y / cellSize)),
        static_cast<int32_t>(std::floor(position.z / cellSize))
    };
}

ParticleQueryDebugData BuildParticleQueryDebugData(
    const std::vector<SoftBodyParticle>& particles,
    const std::vector<SoftBodyTriangleSpatialHashCellDebugInfo>& spatialHashCells,
    float spatialHashCellSize)
{
    ParticleQueryDebugData debugData{};
    if (spatialHashCellSize <= 0.0f)
    {
        return debugData;
    }

    // ========================================================================
    // Particle -> exact 3D query cell
    // ========================================================================
    // GenerateParticleTriangleCandidates()はParticleごとにComputeCellCoord()を1回行い、その3D Cellに
    // 登録済みTriangleだけを最初の候補集合として取得します。Active Cell Snapshotには同じCell座標と
    // TriangleCountが保存されているため、ここで完全に同じ「Bucket lookup直後の候補数」を再現できます。
    //
    // AABB / Topology / Plane / Edge Half-Spaceによるcheap rejectより前の値なので、Profilerの
    // SoftBody.TriangleHash.CellCandidateCountと対応するBroad Phase入口の可視化になります。
    for (const SoftBodyTriangleSpatialHashCellDebugInfo& cell : spatialHashCells)
    {
        debugData.ActiveCellTriangleCounts[
            std::make_tuple(cell.X, cell.Y, cell.Z)] = cell.TriangleCount;
    }

    for (const SoftBodyParticle& particle : particles)
    {
        const auto cellKey = ComputeCellKey(particle.Position, spatialHashCellSize);
        const auto activeCellIterator = debugData.ActiveCellTriangleCounts.find(cellKey);
        if (activeCellIterator == debugData.ActiveCellTriangleCounts.end())
        {
            continue;
        }

        const uint32_t triangleCount = activeCellIterator->second;
        ParticleQueryCellInfo& hitCell = debugData.HitCells[cellKey];
        ++hitCell.ParticleCount;
        hitCell.TriangleCountPerParticle = triangleCount;

        ++debugData.HitParticleCount;
        debugData.CellCandidateCount += static_cast<uint64_t>(triangleCount);
    }

    return debugData;
}

void WriteSpatialHashGrid(
    std::ofstream& stream,
    const ProjectionBounds& bounds,
    float spatialHashCellSize,
    const SoftBodyPhysicsDebugSvgWriter::Settings& settings)
{
    if (settings.DrawSpatialHashGrid == false || spatialHashCellSize <= 0.0f)
    {
        return;
    }

    // ========================================================================
    // Spatial Hash XY Projection
    // ========================================================================
    // SoftBodyTriangleSpatialHashGrid::ComputeCellCoord()は各軸を
    // floor(position / cellSize) してCellを決定します。
    // Browser側でも同じ原点固定境界を再構築することで、Particle / TriangleがどのXY Cellへ
    // 入るかをSolver実装と同じ区切りで確認できます。
    //
    // 3D Spatial HashのZ Layerはこの2D表示では重なるため、境界線自体はXYだけを描画します。
    // Active CellについてはWriteOccupiedSpatialHashCells()でZ Layer情報を集約して重ねます。
    const int32_t minimumCellX =
        static_cast<int32_t>(std::floor(bounds.MinX / spatialHashCellSize));
    const int32_t maximumCellX =
        static_cast<int32_t>(std::floor(bounds.MaxX / spatialHashCellSize));
    const int32_t minimumCellY =
        static_cast<int32_t>(std::floor(bounds.MinY / spatialHashCellSize));
    const int32_t maximumCellY =
        static_cast<int32_t>(std::floor(bounds.MaxY / spatialHashCellSize));

    const float top = ProjectY(bounds.MaxY, bounds, settings);
    const float bottom = ProjectY(bounds.MinY, bounds, settings);
    const float left = ProjectX(bounds.MinX, bounds, settings);
    const float right = ProjectX(bounds.MaxX, bounds, settings);

    stream << "  <g stroke=\"#334155\" stroke-width=\"0.7\" stroke-opacity=\"0.75\">\n";

    for (int32_t cellX = minimumCellX; cellX <= maximumCellX + 1; ++cellX)
    {
        const float worldX = static_cast<float>(cellX) * spatialHashCellSize;
        if (worldX < bounds.MinX || worldX > bounds.MaxX)
        {
            continue;
        }

        const float x = ProjectX(worldX, bounds, settings);
        stream << "    <line x1=\"" << x << "\" y1=\"" << top
               << "\" x2=\"" << x << "\" y2=\"" << bottom << "\"/>\n";
    }

    for (int32_t cellY = minimumCellY; cellY <= maximumCellY + 1; ++cellY)
    {
        const float worldY = static_cast<float>(cellY) * spatialHashCellSize;
        if (worldY < bounds.MinY || worldY > bounds.MaxY)
        {
            continue;
        }

        const float y = ProjectY(worldY, bounds, settings);
        stream << "    <line x1=\"" << left << "\" y1=\"" << y
               << "\" x2=\"" << right << "\" y2=\"" << y << "\"/>\n";
    }

    stream << "  </g>\n";
}

void WriteOccupiedSpatialHashCells(
    std::ofstream& stream,
    const ProjectionBounds& bounds,
    float spatialHashCellSize,
    const std::vector<SoftBodyTriangleSpatialHashCellDebugInfo>& spatialHashCells,
    const SoftBodyPhysicsDebugSvgWriter::Settings& settings)
{
    if (settings.DrawOccupiedSpatialHashCells == false
        || spatialHashCellSize <= 0.0f
        || spatialHashCells.empty() == true)
    {
        return;
    }

    // ========================================================================
    // 3D Active Cell -> XY Heat Map
    // ========================================================================
    // SVGはXY正投影なので、同じ(X,Y)にある複数Z Cellをそのまま描くと完全に重なります。
    // そこでXYごとにTriangle登録数とZ Layer範囲を集約し、1矩形として表示します。
    // 色の濃さは「そのXY列へ何Triangle登録が存在するか」、tooltipはZ Layer数/範囲を表します。
    // これによりClothがZ方向へ折れ、同じXYへ複数層が重なった箇所も2D Viewerから判別できます。
    std::map<std::pair<int32_t, int32_t>, ProjectedOccupiedCell> projectedCells;
    uint64_t maximumTriangleRegistrationCount = 1u;

    for (const SoftBodyTriangleSpatialHashCellDebugInfo& cell : spatialHashCells)
    {
        const std::pair<int32_t, int32_t> key{ cell.X, cell.Y };
        auto [iterator, inserted] = projectedCells.try_emplace(key);
        ProjectedOccupiedCell& projected = iterator->second;

        if (inserted == true)
        {
            projected.MinimumZ = cell.Z;
            projected.MaximumZ = cell.Z;
        }
        else
        {
            projected.MinimumZ = std::min(projected.MinimumZ, cell.Z);
            projected.MaximumZ = std::max(projected.MaximumZ, cell.Z);
        }

        projected.TriangleRegistrationCount += static_cast<uint64_t>(cell.TriangleCount);
        ++projected.ZLayerCount;
        maximumTriangleRegistrationCount = std::max(
            maximumTriangleRegistrationCount,
            projected.TriangleRegistrationCount);
    }

    stream << "  <g stroke=\"#f59e0b\" stroke-width=\"0.8\">\n";

    for (const auto& entry : projectedCells)
    {
        const int32_t cellX = entry.first.first;
        const int32_t cellY = entry.first.second;
        const ProjectedOccupiedCell& projected = entry.second;

        const float worldMinX = static_cast<float>(cellX) * spatialHashCellSize;
        const float worldMaxX = static_cast<float>(cellX + 1) * spatialHashCellSize;
        const float worldMinY = static_cast<float>(cellY) * spatialHashCellSize;
        const float worldMaxY = static_cast<float>(cellY + 1) * spatialHashCellSize;

        // Particle bounds外へExpansionしたCellもHashには存在します。
        // 現在のSVG viewはParticle bounds基準なので、完全に画面外のCellだけ除外し、端で交差するCellは
        // clip相当としてboundsへClampして表示します。
        if (worldMaxX < bounds.MinX
            || worldMinX > bounds.MaxX
            || worldMaxY < bounds.MinY
            || worldMinY > bounds.MaxY)
        {
            continue;
        }

        const float clippedMinX = std::max(worldMinX, bounds.MinX);
        const float clippedMaxX = std::min(worldMaxX, bounds.MaxX);
        const float clippedMinY = std::max(worldMinY, bounds.MinY);
        const float clippedMaxY = std::min(worldMaxY, bounds.MaxY);

        const float x = ProjectX(clippedMinX, bounds, settings);
        const float y = ProjectY(clippedMaxY, bounds, settings);
        const float width = ProjectX(clippedMaxX, bounds, settings) - x;
        const float height = ProjectY(clippedMinY, bounds, settings) - y;

        const float density = static_cast<float>(projected.TriangleRegistrationCount)
            / static_cast<float>(maximumTriangleRegistrationCount);
        const float opacity = 0.08f + density * 0.30f;

        stream << "    <rect x=\"" << x << "\" y=\"" << y
               << "\" width=\"" << std::max(width, 0.0f)
               << "\" height=\"" << std::max(height, 0.0f)
               << "\" fill=\"#f59e0b\" fill-opacity=\"" << opacity << "\">"
               << "<title>Cell XY(" << cellX << ", " << cellY << ")"
               << " | Z Layers: " << projected.ZLayerCount
               << " [" << projected.MinimumZ << ", " << projected.MaximumZ << "]"
               << " | Triangle registrations: " << projected.TriangleRegistrationCount
               << "</title></rect>\n";
    }

    stream << "  </g>\n";
}

void WriteParticleQueryCells(
    std::ofstream& stream,
    const ProjectionBounds& bounds,
    float spatialHashCellSize,
    const ParticleQueryDebugData& debugData,
    const SoftBodyPhysicsDebugSvgWriter::Settings& settings)
{
    if (settings.DrawParticleQueryCells == false
        || spatialHashCellSize <= 0.0f
        || debugData.HitCells.empty() == true)
    {
        return;
    }

    // ========================================================================
    // Particle Query Cell Overlay
    // ========================================================================
    // シアン枠は「Particleが問い合わせた3D Cellに実際にTriangle Bucketが存在した」ことを表します。
    // 同じXYでもZが違えば別Cellですが、2D投影上は重なるためtooltipへ正確なZを残します。
    // CellCandidateはParticleCount * TriangleCountで、cheap reject前に生成側が走査するPair数そのものです。
    stream << "  <g fill=\"none\" stroke=\"#22d3ee\" stroke-width=\"1.6\" stroke-opacity=\"0.95\">\n";

    for (const auto& entry : debugData.HitCells)
    {
        const int32_t cellX = std::get<0>(entry.first);
        const int32_t cellY = std::get<1>(entry.first);
        const int32_t cellZ = std::get<2>(entry.first);
        const ParticleQueryCellInfo& queryInfo = entry.second;

        const float worldMinX = static_cast<float>(cellX) * spatialHashCellSize;
        const float worldMaxX = static_cast<float>(cellX + 1) * spatialHashCellSize;
        const float worldMinY = static_cast<float>(cellY) * spatialHashCellSize;
        const float worldMaxY = static_cast<float>(cellY + 1) * spatialHashCellSize;

        if (worldMaxX < bounds.MinX
            || worldMinX > bounds.MaxX
            || worldMaxY < bounds.MinY
            || worldMinY > bounds.MaxY)
        {
            continue;
        }

        const float clippedMinX = std::max(worldMinX, bounds.MinX);
        const float clippedMaxX = std::min(worldMaxX, bounds.MaxX);
        const float clippedMinY = std::max(worldMinY, bounds.MinY);
        const float clippedMaxY = std::min(worldMaxY, bounds.MaxY);

        const float x = ProjectX(clippedMinX, bounds, settings);
        const float y = ProjectY(clippedMaxY, bounds, settings);
        const float width = ProjectX(clippedMaxX, bounds, settings) - x;
        const float height = ProjectY(clippedMinY, bounds, settings) - y;
        const uint64_t candidateCount = static_cast<uint64_t>(queryInfo.ParticleCount)
            * static_cast<uint64_t>(queryInfo.TriangleCountPerParticle);

        stream << "    <rect x=\"" << x << "\" y=\"" << y
               << "\" width=\"" << std::max(width, 0.0f)
               << "\" height=\"" << std::max(height, 0.0f) << "\">"
               << "<title>Particle Query Cell (" << cellX << ", " << cellY << ", " << cellZ << ")"
               << " | Particles: " << queryInfo.ParticleCount
               << " | Triangles/Particle: " << queryInfo.TriangleCountPerParticle
               << " | Cell Candidates: " << candidateCount
               << "</title></rect>\n";
    }

    stream << "  </g>\n";
}

void WriteParticleQueryMarkers(
    std::ofstream& stream,
    const std::vector<SoftBodyParticle>& particles,
    const ProjectionBounds& bounds,
    float spatialHashCellSize,
    const ParticleQueryDebugData& debugData,
    const SoftBodyPhysicsDebugSvgWriter::Settings& settings)
{
    if (settings.DrawParticleQueryCells == false || spatialHashCellSize <= 0.0f)
    {
        return;
    }

    // Bucket HitしたParticleをシアンの外周で囲みます。
    // 通常Particle色はZ変位を保持し、その外側だけBroad Phase Query状態を重ねます。
    stream << "  <g fill=\"none\" stroke=\"#22d3ee\" stroke-width=\"1.4\">\n";

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        const SoftBodyParticle& particle = particles[particleIndex];
        const auto cellKey = ComputeCellKey(particle.Position, spatialHashCellSize);
        const auto activeCellIterator = debugData.ActiveCellTriangleCounts.find(cellKey);
        if (activeCellIterator == debugData.ActiveCellTriangleCounts.end())
        {
            continue;
        }

        stream << "    <circle cx=\"" << ProjectX(particle.Position.x, bounds, settings)
               << "\" cy=\"" << ProjectY(particle.Position.y, bounds, settings)
               << "\" r=\"6.0\"><title>Query Particle " << particleIndex
               << " | Cell(" << std::get<0>(cellKey) << ", "
               << std::get<1>(cellKey) << ", " << std::get<2>(cellKey) << ")"
               << " | Raw cell triangles: " << activeCellIterator->second
               << "</title></circle>\n";
    }

    stream << "  </g>\n";
}
} // namespace

bool SoftBodyPhysicsDebugSvgWriter::Write(
    const std::filesystem::path& filePath,
    const std::vector<SoftBodyParticle>& particles,
    const std::vector<uint32_t>& triangleIndices,
    const SoftBodyParticleTriangleCollisionStatistics& statistics,
    float spatialHashCellSize,
    const std::vector<SoftBodyTriangleSpatialHashCellDebugInfo>& spatialHashCells,
    const Settings& settings)
{
    if (particles.empty() == true)
    {
        return false;
    }

    const std::filesystem::path parentPath = filePath.parent_path();
    if (parentPath.empty() == false)
    {
        std::error_code errorCode;
        std::filesystem::create_directories(parentPath, errorCode);
        if (errorCode)
        {
            std::cerr << "[SoftBodyPhysicsDebugSvgWriter] 出力先を作成できませんでした: "
                      << parentPath.string() << '\n';
            return false;
        }
    }

    std::ofstream stream(filePath, std::ios::binary | std::ios::trunc);
    if (stream.is_open() == false)
    {
        return false;
    }

    const ProjectionBounds bounds = ComputeBounds(particles);
    const ParticleQueryDebugData particleQueryDebugData = BuildParticleQueryDebugData(
        particles,
        spatialHashCells,
        spatialHashCellSize);

    stream << std::fixed << std::setprecision(3);
    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << settings.Width
           << "\" height=\"" << settings.Height << "\" viewBox=\"0 0 "
           << settings.Width << ' ' << settings.Height << "\">\n";
    stream << "  <rect width=\"100%\" height=\"100%\" fill=\"#0f172a\"/>\n";
    stream << "  <text x=\"48\" y=\"46\" fill=\"#f8fafc\" font-family=\"sans-serif\" font-size=\"26\" font-weight=\"bold\">Raven SoftBody Physics Debug</text>\n";
    stream << "  <text x=\"48\" y=\"76\" fill=\"#94a3b8\" font-family=\"monospace\" font-size=\"14\">Particles: "
           << particles.size() << " | CellSize: " << spatialHashCellSize
           << " | ActiveCells3D: " << spatialHashCells.size()
           << " | QueryHits: " << particleQueryDebugData.HitParticleCount
           << " | CellCandidates: " << particleQueryDebugData.CellCandidateCount
           << "</text>\n";
    stream << "  <text x=\"48\" y=\"98\" fill=\"#94a3b8\" font-family=\"monospace\" font-size=\"14\">Solver Candidate: "
           << statistics.CandidateCount
           << " | NarrowPhase: " << statistics.NarrowPhaseCount
           << " | Correction: " << statistics.PositionCorrectionCount << "</text>\n";

    // Occupied CellをGrid境界より後、Cloth Triangleより前に描きます。
    // Heat Mapは背景情報として残しつつ、Particle/Triangleの形状を最前面で確認できます。
    WriteSpatialHashGrid(stream, bounds, spatialHashCellSize, settings);
    WriteOccupiedSpatialHashCells(stream, bounds, spatialHashCellSize, spatialHashCells, settings);
    WriteParticleQueryCells(
        stream,
        bounds,
        spatialHashCellSize,
        particleQueryDebugData,
        settings);

    if (settings.DrawTriangles == true)
    {
        stream << "  <g fill=\"#1e3a5f\" fill-opacity=\"0.32\" stroke=\"#475569\" stroke-width=\"0.7\">\n";
        for (std::size_t index = 0u; index + 2u < triangleIndices.size(); index += 3u)
        {
            const uint32_t indexA = triangleIndices[index];
            const uint32_t indexB = triangleIndices[index + 1u];
            const uint32_t indexC = triangleIndices[index + 2u];
            if (indexA >= particles.size()
                || indexB >= particles.size()
                || indexC >= particles.size())
            {
                continue;
            }

            const math::Vec3& a = particles[indexA].Position;
            const math::Vec3& b = particles[indexB].Position;
            const math::Vec3& c = particles[indexC].Position;
            stream << "    <polygon points=\""
                   << ProjectX(a.x, bounds, settings) << ',' << ProjectY(a.y, bounds, settings) << ' '
                   << ProjectX(b.x, bounds, settings) << ',' << ProjectY(b.y, bounds, settings) << ' '
                   << ProjectX(c.x, bounds, settings) << ',' << ProjectY(c.y, bounds, settings)
                   << "\"/>\n";
        }
        stream << "  </g>\n";
    }

    if (settings.DrawParticles == true)
    {
        stream << "  <g>\n";
        for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
        {
            const SoftBodyParticle& particle = particles[particleIndex];
            const float depth = NormalizeDepth(particle.Position.z, bounds);

            // Z変位を青->橙の補間値としてSVGへ直接埋め込み、2D投影で失われる奥行きを色で残します。
            const int red = static_cast<int>(80.0f + depth * 170.0f);
            const int green = static_cast<int>(160.0f - depth * 60.0f);
            const int blue = static_cast<int>(240.0f - depth * 150.0f);

            stream << "    <circle cx=\"" << ProjectX(particle.Position.x, bounds, settings)
                   << "\" cy=\"" << ProjectY(particle.Position.y, bounds, settings)
                   << "\" r=\"" << (particle.IsFixed() == true ? 4.5f : 2.4f) << "\" fill=\"rgb(";

            if (particle.IsFixed() == true)
            {
                stream << "250,204,21";
            }
            else
            {
                stream << red << ',' << green << ',' << blue;
            }

            stream << ")\"><title>Particle " << particleIndex
                   << " | Pos(" << particle.Position.x << ", " << particle.Position.y << ", "
                   << particle.Position.z << ")</title></circle>\n";
        }
        stream << "  </g>\n";
    }

    // Query Hit Particleのシアン外周は通常Particleより後に描き、判別しやすくします。
    WriteParticleQueryMarkers(
        stream,
        particles,
        bounds,
        spatialHashCellSize,
        particleQueryDebugData,
        settings);

    stream << "  <text x=\"48\" y=\"" << (settings.Height - 78u)
           << "\" fill=\"#22d3ee\" font-family=\"monospace\" font-size=\"12\">Cyan: particle query hit cell / raw CellCandidate source</text>\n";
    stream << "  <text x=\"48\" y=\"" << (settings.Height - 60u)
           << "\" fill=\"#f59e0b\" font-family=\"monospace\" font-size=\"12\">Orange: occupied Particle-Triangle hash cells / opacity = triangle registrations</text>\n";
    stream << "  <text x=\"48\" y=\"" << (settings.Height - 42u)
           << "\" fill=\"#64748b\" font-family=\"monospace\" font-size=\"12\">Grid: Particle-Triangle Spatial Hash XY boundaries / origin aligned</text>\n";
    stream << "  <text x=\"48\" y=\"" << (settings.Height - 24u)
           << "\" fill=\"#64748b\" font-family=\"monospace\" font-size=\"12\">XY orthographic projection / particle Z is encoded as color</text>\n";
    stream << "</svg>\n";
    return stream.good();
}

} // namespace ph
} // namespace Raven
