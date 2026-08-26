#include "Raven/Physics/SoftBody/Debug/SoftBodyPhysicsDebugSvgWriter.h"

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
    float MinZ = 0.0f;
    float MaxZ = 0.0f;
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

float ProjectX(float x, const ProjectionBounds& bounds, const SoftBodyPhysicsDebugSvgWriter::Settings& settings)
{
    const float drawableWidth = std::max(static_cast<float>(settings.Width) - settings.Padding * 2.0f, 1.0f);
    return settings.Padding + ((x - bounds.MinX) / SafeRange(bounds.MinX, bounds.MaxX)) * drawableWidth;
}

float ProjectY(float y, const ProjectionBounds& bounds, const SoftBodyPhysicsDebugSvgWriter::Settings& settings)
{
    const float drawableHeight = std::max(static_cast<float>(settings.Height) - settings.Padding * 2.0f - 150.0f, 1.0f);

    // SVGは下向きが+Yなので、Physicsの+Yが画面上方向になるよう反転します。
    return settings.Padding + 120.0f
        + (1.0f - ((y - bounds.MinY) / SafeRange(bounds.MinY, bounds.MaxY))) * drawableHeight;
}

float NormalizeDepth(float z, const ProjectionBounds& bounds)
{
    return (z - bounds.MinZ) / SafeRange(bounds.MinZ, bounds.MaxZ);
}
} // namespace

bool SoftBodyPhysicsDebugSvgWriter::Write(
    const std::filesystem::path& filePath,
    const std::vector<SoftBodyParticle>& particles,
    const std::vector<uint32_t>& triangleIndices,
    const SoftBodyParticleTriangleCollisionStatistics& statistics,
    float spatialHashCellSize,
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

    stream << std::fixed << std::setprecision(3);
    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << settings.Width
           << "\" height=\"" << settings.Height << "\" viewBox=\"0 0 "
           << settings.Width << ' ' << settings.Height << "\">\n";
    stream << "  <rect width=\"100%\" height=\"100%\" fill=\"#0f172a\"/>\n";
    stream << "  <text x=\"48\" y=\"46\" fill=\"#f8fafc\" font-family=\"sans-serif\" font-size=\"26\" font-weight=\"bold\">Raven SoftBody Physics Debug</text>\n";
    stream << "  <text x=\"48\" y=\"76\" fill=\"#94a3b8\" font-family=\"monospace\" font-size=\"14\">Particles: "
           << particles.size() << " | CellSize: " << spatialHashCellSize
           << " | Candidate: " << statistics.CandidateCount
           << " | NarrowPhase: " << statistics.NarrowPhaseCount
           << " | Correction: " << statistics.PositionCorrectionCount << "</text>\n";

    if (settings.DrawTriangles == true)
    {
        stream << "  <g fill=\"#1e3a5f\" fill-opacity=\"0.32\" stroke=\"#475569\" stroke-width=\"0.7\">\n";
        for (std::size_t index = 0u; index + 2u < triangleIndices.size(); index += 3u)
        {
            const uint32_t indexA = triangleIndices[index];
            const uint32_t indexB = triangleIndices[index + 1u];
            const uint32_t indexC = triangleIndices[index + 2u];
            if (indexA >= particles.size() || indexB >= particles.size() || indexC >= particles.size())
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

    stream << "  <text x=\"48\" y=\"" << (settings.Height - 24u)
           << "\" fill=\"#64748b\" font-family=\"monospace\" font-size=\"12\">XY orthographic projection / particle Z is encoded as color</text>\n";
    stream << "</svg>\n";
    return stream.good();
}

} // namespace ph
} // namespace Raven
