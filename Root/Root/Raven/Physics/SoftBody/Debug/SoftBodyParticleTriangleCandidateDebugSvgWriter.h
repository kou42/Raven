#pragma once

#include <filesystem>
#include <vector>

#include "Raven/Physics/SoftBody/Debug/SoftBodyParticleTriangleCandidateDebugSnapshot.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// SoftBodyParticleTriangleCandidateDebugSvgWriter
// ============================================================================
// Particle-Triangle Broad Phaseのcheap reject funnelだけを詳しく確認するための専用SVG Writerです。
// 既存SoftBodyPhysicsDebugSvgWriterとは分離し、Spatial Hash全体表示とReject理由表示の責務を混ぜません。
//
// 各RecordについてQuery ParticleとTriangle重心を線で結び、Reject理由ごとに色分けします。
// Browser側では通常Physics表示と並べて確認することを想定しています。
class SoftBodyParticleTriangleCandidateDebugSvgWriter
{
public:
    static bool Write(
        const std::filesystem::path& filePath,
        const std::vector<SoftBodyParticle>& particles,
        const std::vector<uint32_t>& triangleIndices,
        const SoftBodyParticleTriangleCandidateDebugSnapshot& snapshot);
};

} // namespace ph
} // namespace Raven
