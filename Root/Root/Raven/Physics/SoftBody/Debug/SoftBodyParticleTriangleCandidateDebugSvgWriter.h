#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
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
    struct Settings
    {
        // InvalidIndexなら全Particleを表示します。
        // ParticleIndexを指定した場合、そのParticleがQueryになっているPairだけを描画します。
        static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();
        uint32_t ParticleIndex = InvalidIndex;

        // InvalidIndexなら全Triangleを表示します。
        // TriangleIndexを指定した場合、そのTriangleに対するPairだけを描画します。
        // ParticleIndexと同時指定した場合はAND条件になり、1 Pairを精密に追跡できます。
        uint32_t TriangleIndex = InvalidIndex;

        // trueの場合、BrowserDebugServerが保持する最新FilterをこのSettingsへ上書きして使用します。
        // Browser Debug Viewerからの選択をRuntime SVGへ反映する通常経路ではtrueのまま使います。
        // Headless TestなどServerと切り離して明示Filterだけを検証したい場合はfalseにできます。
        bool UseBrowserFilterState = true;
    };

    static bool Write(
        const std::filesystem::path& filePath,
        const std::vector<SoftBodyParticle>& particles,
        const std::vector<uint32_t>& triangleIndices,
        const SoftBodyParticleTriangleCandidateDebugSnapshot& snapshot,
        const Settings& settings = Settings{});
};

} // namespace ph
} // namespace Raven
