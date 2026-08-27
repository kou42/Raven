#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "Raven/Physics/SoftBody/SoftBodyParticle.h"
#include "Raven/Physics/SoftBody/SoftBodySolver.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// SoftBodyPhysicsDebugSvgWriter
// ============================================================================
// SoftBody Solverが持つ実データをブラウザで確認できるSVGへ変換します。
// Renderer / OpenGLには依存させず、Physics側のParticleとTopologyだけを入力にすることで、
// 将来Headless TestやProfiler Captureから同じ可視化処理を再利用できるようにします。
//
// 現段階ではClothを正面(XY平面)へ正投影します。
// Z方向の変位はParticle色へ反映するため、Sphere衝突などでClothが手前/奥へ変形した場合も
// 2D SVG上である程度把握できます。
class SoftBodyPhysicsDebugSvgWriter
{
public:
    struct Settings
    {
        uint32_t Width = 1100u;
        uint32_t Height = 760u;
        float Padding = 48.0f;

        // Particle-Triangle Broad Phaseで使用しているCell Sizeを基準に、
        // World原点へ固定されたXY Spatial Hash境界を描画します。
        // ここでは3D GridをXYへ投影した境界だけを描き、Z LayerごとのOccupied Cell可視化は
        // 次段階でGrid内部Snapshot APIを追加して重ねる想定です。
        bool DrawSpatialHashGrid = true;

        // Triangle面を表示するとParticle位置だけでなくCloth形状を把握しやすくなります。
        bool DrawTriangles = true;

        // Particleの固定点は通常Particleと別色で表示します。
        bool DrawParticles = true;
    };

    // triangleIndicesは3要素ごとに1 Triangleとして扱います。
    // Mesh indexでもSoftBody topology indexでも、Particle indexと一致していれば利用できます。
    static bool Write(
        const std::filesystem::path& filePath,
        const std::vector<SoftBodyParticle>& particles,
        const std::vector<uint32_t>& triangleIndices,
        const SoftBodyParticleTriangleCollisionStatistics& statistics,
        float spatialHashCellSize,
        const Settings& settings = Settings{});
};

} // namespace ph
} // namespace Raven
