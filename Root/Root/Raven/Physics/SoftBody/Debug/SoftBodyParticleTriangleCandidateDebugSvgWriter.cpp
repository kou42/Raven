#include "Raven/Physics/SoftBody/Debug/SoftBodyParticleTriangleCandidateDebugSvgWriter.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>

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

    for (const SoftBodyParticle& particle : particles)
    {
        bounds.MinX = std::min(bounds.MinX, particle.Position.x);
        bounds.MaxX = std::max(bounds.MaxX, particle.Position.x);
        bounds.MinY = std::min(bounds.MinY, particle.Position.y);
        bounds.MaxY = std::max(bounds.MaxY, particle.Position.y);
    }

    return bounds;
}

float SafeRange(float minimum, float maximum)
{
    return std::max(maximum - minimum, 1.0e-5f);
}

float ProjectX(float x, const ProjectionBounds& bounds)
{
    constexpr float Left = 46.0f;
    constexpr float Right = 1054.0f;
    return Left + ((x - bounds.MinX) / SafeRange(bounds.MinX, bounds.MaxX)) * (Right - Left);
}

float ProjectY(float y, const ProjectionBounds& bounds)
{
    constexpr float Top = 160.0f;
    constexpr float Bottom = 704.0f;
    return Top + (1.0f - ((y - bounds.MinY) / SafeRange(bounds.MinY, bounds.MaxY))) * (Bottom - Top);
}

const char* GetReasonColor(SoftBodyParticleTriangleCandidateDebugReason reason)
{
    switch (reason)
    {
    case SoftBodyParticleTriangleCandidateDebugReason::AABBReject:
        return "#ef4444";
    case SoftBodyParticleTriangleCandidateDebugReason::TopologyReject:
        return "#a855f7";
    case SoftBodyParticleTriangleCandidateDebugReason::PlaneReject:
        return "#eab308";
    case SoftBodyParticleTriangleCandidateDebugReason::EdgeReject:
        return "#f97316";
    case SoftBodyParticleTriangleCandidateDebugReason::NarrowPhaseCandidate:
        return "#22c55e";
    }

    return "#94a3b8";
}

const char* GetReasonName(SoftBodyParticleTriangleCandidateDebugReason reason)
{
    switch (reason)
    {
    case SoftBodyParticleTriangleCandidateDebugReason::AABBReject:
        return "AABB Reject";
    case SoftBodyParticleTriangleCandidateDebugReason::TopologyReject:
        return "Topology Reject";
    case SoftBodyParticleTriangleCandidateDebugReason::PlaneReject:
        return "Plane Reject";
    case SoftBodyParticleTriangleCandidateDebugReason::EdgeReject:
        return "Edge Reject";
    case SoftBodyParticleTriangleCandidateDebugReason::NarrowPhaseCandidate:
        return "NarrowPhase Candidate";
    }

    return "Unknown";
}

bool TryGetTriangleParticleIndices(
    const std::vector<uint32_t>& triangleIndices,
    uint32_t triangleIndex,
    uint32_t& outA,
    uint32_t& outB,
    uint32_t& outC)
{
    const std::size_t baseIndex = static_cast<std::size_t>(triangleIndex) * 3u;
    if (baseIndex + 2u >= triangleIndices.size())
    {
        return false;
    }

    outA = triangleIndices[baseIndex];
    outB = triangleIndices[baseIndex + 1u];
    outC = triangleIndices[baseIndex + 2u];
    return true;
}
} // namespace

bool SoftBodyParticleTriangleCandidateDebugSvgWriter::Write(
    const std::filesystem::path& filePath,
    const std::vector<SoftBodyParticle>& particles,
    const std::vector<uint32_t>& triangleIndices,
    const SoftBodyParticleTriangleCandidateDebugSnapshot& snapshot)
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
            std::cerr << "[SoftBodyParticleTriangleCandidateDebugSvgWriter] 出力先を作成できませんでした: "
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
    const SoftBodyParticleTriangleCandidateDebugStatistics& statistics = snapshot.Statistics;

    stream << std::fixed << std::setprecision(3);
    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1100\" height=\"760\" viewBox=\"0 0 1100 760\">\n";
    stream << "  <rect width=\"1100\" height=\"760\" fill=\"#0f172a\"/>\n";
    stream << "  <text x=\"46\" y=\"42\" fill=\"#f8fafc\" font-family=\"sans-serif\" font-size=\"25\" font-weight=\"bold\">Particle-Triangle Reject Funnel</text>\n";
    stream << "  <text x=\"46\" y=\"72\" fill=\"#94a3b8\" font-family=\"monospace\" font-size=\"13\">Cell Candidates: "
           << statistics.CellCandidateCount
           << " | AABB: " << statistics.AABBRejectCount
           << " | Topology: " << statistics.TopologyRejectCount
           << " | Plane: " << statistics.PlaneRejectCount
           << " | Edge: " << statistics.EdgeRejectCount
           << " | Narrow: " << statistics.NarrowPhaseCandidateCount
           << "</text>\n";

    // ========================================================================
    // Legend
    // ========================================================================
    // Funnel順に左から並べます。通常Candidate生成コードと同じ順序にすることで、
    // 色を見たときに「どこまで処理が進んだ候補か」を直感的に比較できます。
    const SoftBodyParticleTriangleCandidateDebugReason legendReasons[] = {
        SoftBodyParticleTriangleCandidateDebugReason::AABBReject,
        SoftBodyParticleTriangleCandidateDebugReason::TopologyReject,
        SoftBodyParticleTriangleCandidateDebugReason::PlaneReject,
        SoftBodyParticleTriangleCandidateDebugReason::EdgeReject,
        SoftBodyParticleTriangleCandidateDebugReason::NarrowPhaseCandidate
    };

    float legendX = 46.0f;
    for (SoftBodyParticleTriangleCandidateDebugReason reason : legendReasons)
    {
        stream << "  <rect x=\"" << legendX << "\" y=\"96\" width=\"13\" height=\"13\" fill=\""
               << GetReasonColor(reason) << "\"/>\n";
        stream << "  <text x=\"" << (legendX + 19.0f)
               << "\" y=\"108\" fill=\"#cbd5e1\" font-family=\"monospace\" font-size=\"12\">"
               << GetReasonName(reason) << "</text>\n";
        legendX += 205.0f;
    }

    // Cloth全体を薄く描き、Reject線の位置関係を把握するための背景にします。
    stream << "  <g fill=\"#1e293b\" fill-opacity=\"0.30\" stroke=\"#475569\" stroke-width=\"0.55\">\n";
    for (std::size_t index = 0u; index + 2u < triangleIndices.size(); index += 3u)
    {
        const uint32_t aIndex = triangleIndices[index];
        const uint32_t bIndex = triangleIndices[index + 1u];
        const uint32_t cIndex = triangleIndices[index + 2u];
        if (aIndex >= particles.size() || bIndex >= particles.size() || cIndex >= particles.size())
        {
            continue;
        }

        const math::Vec3& a = particles[aIndex].Position;
        const math::Vec3& b = particles[bIndex].Position;
        const math::Vec3& c = particles[cIndex].Position;
        stream << "    <polygon points=\""
               << ProjectX(a.x, bounds) << ',' << ProjectY(a.y, bounds) << ' '
               << ProjectX(b.x, bounds) << ',' << ProjectY(b.y, bounds) << ' '
               << ProjectX(c.x, bounds) << ',' << ProjectY(c.y, bounds)
               << "\"/>\n";
    }
    stream << "  </g>\n";

    // ========================================================================
    // Candidate Pair Overlay
    // ========================================================================
    // Query Particleから対象Triangle重心へ線を引きます。
    // 同じParticleが複数Triangleを評価する状況でも、色と線の向きからPair単位で追跡できます。
    for (const SoftBodyParticleTriangleCandidateDebugInfo& record : snapshot.Records)
    {
        if (record.ParticleIndex >= particles.size())
        {
            continue;
        }

        uint32_t triangleA = 0u;
        uint32_t triangleB = 0u;
        uint32_t triangleC = 0u;
        if (TryGetTriangleParticleIndices(
                triangleIndices,
                record.TriangleIndex,
                triangleA,
                triangleB,
                triangleC) == false)
        {
            continue;
        }

        if (triangleA >= particles.size()
            || triangleB >= particles.size()
            || triangleC >= particles.size())
        {
            continue;
        }

        const math::Vec3& particlePosition = particles[record.ParticleIndex].Position;
        const math::Vec3 triangleCenter =
            (particles[triangleA].Position
                + particles[triangleB].Position
                + particles[triangleC].Position) / 3.0f;
        const char* color = GetReasonColor(record.Reason);

        stream << "  <line x1=\"" << ProjectX(particlePosition.x, bounds)
               << "\" y1=\"" << ProjectY(particlePosition.y, bounds)
               << "\" x2=\"" << ProjectX(triangleCenter.x, bounds)
               << "\" y2=\"" << ProjectY(triangleCenter.y, bounds)
               << "\" stroke=\"" << color
               << "\" stroke-width=\"1.15\" stroke-opacity=\"0.34\">"
               << "<title>Particle " << record.ParticleIndex
               << " -> Triangle " << record.TriangleIndex
               << " | " << GetReasonName(record.Reason)
               << "</title></line>\n";

        stream << "  <circle cx=\"" << ProjectX(particlePosition.x, bounds)
               << "\" cy=\"" << ProjectY(particlePosition.y, bounds)
               << "\" r=\"3.8\" fill=\"none\" stroke=\"" << color
               << "\" stroke-width=\"1.2\" stroke-opacity=\"0.72\">"
               << "<title>Particle " << record.ParticleIndex
               << " | Triangle " << record.TriangleIndex
               << " | " << GetReasonName(record.Reason)
               << "</title></circle>\n";
    }

    stream << "  <text x=\"46\" y=\"738\" fill=\"#64748b\" font-family=\"monospace\" font-size=\"12\">Line: query particle -> candidate triangle center / hover for pair and reject reason</text>\n";
    stream << "</svg>\n";
    return stream.good();
}

} // namespace ph
} // namespace Raven
