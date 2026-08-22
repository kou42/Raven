#include <cassert>
#include <cmath>

#include "Raven/Physics/SoftBody/SoftBodyJelly.h"
#include "Raven/Physics/SoftBody/SoftBodyJellySurface.h"
#include "Raven/Renderer/Mesh/Deformation/SoftBodyJellyMeshDeformer.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven::ph::tests
{

// ============================================================================
// Soft Body Jelly Surface Self Tests
// ============================================================================
// Tetrahedral Jellyから内部共有Faceを除外して外表面だけを抽出できること、
// Surface windingが外向きであること、Renderer用Dynamic Geometryへ変換できることを確認します。
void RunSoftBodyJellySurfaceSelfTests()
{
    SoftBodySolver solver{};
    solver.SetGravity({ 0.0f, 0.0f, 0.0f });

    SoftBodyJellySettings settings{};
    settings.CellsX = 1u;
    settings.CellsY = 1u;
    settings.CellsZ = 1u;
    settings.Width = 1.0f;
    settings.Height = 1.0f;
    settings.Depth = 1.0f;
    settings.InverseMass = 1.0f;

    SoftBodyJelly jelly = SoftBodyJellyBuilder::Build(solver, settings);
    const SoftBodyJellySurface surface = SoftBodyJellySurfaceBuilder::Build(jelly);

    // 単一Cubeの8 Particleはすべて外表面上にあります。
    assert(surface.SurfaceParticleIndices.size() == 8u);

    // Cube外表面は6 Quad、各Quadが2 Triangleなので12 Triangleです。
    // 6-Tet内部で共有されるFaceはOccurrenceCount == 2として全て除外されます。
    assert(surface.Triangles.size() == 12u);

    const std::vector<SoftBodyParticle>& particles = solver.GetParticles();

    // ========================================================================
    // Outward Winding
    // ========================================================================
    // Cube中心は原点なので、Face中心へ向かうベクトルとFace Normalのdotが正なら外向きです。
    for (const SoftBodyJellySurfaceTriangle& triangle : surface.Triangles)
    {
        const math::Vec3& a = particles[triangle.ParticleA].Position;
        const math::Vec3& b = particles[triangle.ParticleB].Position;
        const math::Vec3& c = particles[triangle.ParticleC].Position;

        const math::Vec3 faceNormal = math::Vec3::Cross(b - a, c - a);
        const math::Vec3 faceCenter = (a + b + c) / 3.0f;

        assert(faceNormal.LengthSq() > math::Epsilon * math::Epsilon);
        assert(math::Vec3::Dot(faceNormal, faceCenter) > 0.0f);
    }

    // ========================================================================
    // Renderer Dynamic Geometry
    // ========================================================================
    SoftBodyJellyMeshDeformer deformer(solver, surface);
    const Ref<MeshGeometry> geometry = deformer.CreateGeometry();

    assert(geometry != nullptr);
    assert(geometry->GetGeometryUsage() == GeometryUsage::Dynamic);
    assert(geometry->GetTopologyUsage() == TopologyUsage::Fixed);
    assert(geometry->GetVertices().size() == 8u);
    assert(geometry->GetIndices().size() == 36u);

    // Cube cornerでは隣接面Normalの平均になるため軸単位Vectorではありませんが、
    // 正規化済みであることを確認すればNormal再計算経路の回帰テストになります。
    for (const MeshVertex& vertex : geometry->GetVertices())
    {
        const float normalLength = vertex.Normal.Length();
        assert(std::abs(normalLength - 1.0f) < 0.0001f);
    }

    // ========================================================================
    // Multi-cell Surface Count
    // ========================================================================
    // 2x2x2 Cellでは3x3x3=27 Particleのうち中心1点だけが内部Particleです。
    SoftBodySolver multiCellSolver{};
    SoftBodyJellySettings multiSettings{};
    multiSettings.CellsX = 2u;
    multiSettings.CellsY = 2u;
    multiSettings.CellsZ = 2u;

    const SoftBodyJelly multiJelly = SoftBodyJellyBuilder::Build(multiCellSolver, multiSettings);
    const SoftBodyJellySurface multiSurface = SoftBodyJellySurfaceBuilder::Build(multiJelly);

    assert(multiSurface.SurfaceParticleIndices.size() == 26u);

    // 6面 * 2x2 Quad/面 * 2 Triangle/Quad = 48 Triangleです。
    assert(multiSurface.Triangles.size() == 48u);
}

} // namespace Raven::ph::tests
