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
        bool DrawSpatialHashGrid = true;

        // 実際のHash BuildでTriangleが登録されたActive CellをXYへ投影して重ねます。
        // 同じXYに複数Z Layerが存在する場合は1矩形へ集約し、Z Layer数とTriangle登録総数を
        // tooltipで確認できるようにします。
        bool DrawOccupiedSpatialHashCells = true;

        // 各Particleが現在問い合わせる3D CellをActive Cell Snapshotと照合し、
        // 実際にBucketへHitしたQuery Cellをシアン枠で表示します。
        // tooltipにはそのCellからBroad Phaseの最初の段階で取得されるTriangle数を表示します。
        bool DrawParticleQueryCells = true;

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
        const std::vector<SoftBodyTriangleSpatialHashCellDebugInfo>& spatialHashCells,
        const Settings& settings = Settings{});
};

} // namespace ph
} // namespace Raven
