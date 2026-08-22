#include "Raven/Physics/SoftBody/SoftBodyJellySurface.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace Raven
{
namespace ph
{
namespace
{

struct SurfaceFaceKey
{
    std::array<uint32_t, 3u> Particles{};

    bool operator==(const SurfaceFaceKey& other) const
    {
        return Particles == other.Particles;
    }
};

struct SurfaceFaceKeyHasher
{
    std::size_t operator()(const SurfaceFaceKey& key) const
    {
        std::size_t hash = static_cast<std::size_t>(key.Particles[0]);
        hash ^= static_cast<std::size_t>(key.Particles[1]) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        hash ^= static_cast<std::size_t>(key.Particles[2]) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        return hash;
    }
};

struct SurfaceFaceRecord
{
    SoftBodyJellySurfaceTriangle Triangle{};
    uint32_t OccurrenceCount = 0u;
};

SurfaceFaceKey MakeFaceKey(uint32_t particleA, uint32_t particleB, uint32_t particleC)
{
    SurfaceFaceKey key{};
    key.Particles = { particleA, particleB, particleC };
    std::sort(key.Particles.begin(), key.Particles.end());
    return key;
}

void RegisterFace(
    std::unordered_map<SurfaceFaceKey, SurfaceFaceRecord, SurfaceFaceKeyHasher>& faces,
    uint32_t particleA,
    uint32_t particleB,
    uint32_t particleC)
{
    const SurfaceFaceKey key = MakeFaceKey(particleA, particleB, particleC);
    auto [iterator, inserted] = faces.emplace(key, SurfaceFaceRecord{});

    SurfaceFaceRecord& record = iterator->second;
    if (inserted)
    {
        record.Triangle = { particleA, particleB, particleC };
    }

    ++record.OccurrenceCount;
}

} // namespace

SoftBodyJellySurface SoftBodyJellySurfaceBuilder::Build(const SoftBodyJelly& jelly)
{
    SoftBodyJellySurface surface{};

    std::unordered_map<SurfaceFaceKey, SurfaceFaceRecord, SurfaceFaceKeyHasher> faces;
    faces.reserve(jelly.Tetrahedra.size() * 4u);

    // ========================================================================
    // Tetrahedron Face Registration
    // ========================================================================
    // SoftBodyJellyBuilderは全Tetを正の符号付き体積になるorientationで生成します。
    // 正向き四面体 (p0,p1,p2,p3) の境界を外向きwindingで書くと次の4枚です。
    //
    //   opposite p0 : (p1, p2, p3)
    //   opposite p1 : (p0, p3, p2)
    //   opposite p2 : (p0, p1, p3)
    //   opposite p3 : (p0, p2, p1)
    //
    // これは oriented simplex の境界
    //
    //   [123] - [023] + [013] - [012]
    //
    // に対応します。同じFaceを隣接Tetが逆向きに1回ずつ持つため、Particle集合だけをKeyにして
    // OccurrenceCount == 2 のFaceを内部面として除外できます。
    for (const SoftBodyTetrahedron& tetrahedron : jelly.Tetrahedra)
    {
        const uint32_t p0 = tetrahedron.Particle0;
        const uint32_t p1 = tetrahedron.Particle1;
        const uint32_t p2 = tetrahedron.Particle2;
        const uint32_t p3 = tetrahedron.Particle3;

        RegisterFace(faces, p1, p2, p3);
        RegisterFace(faces, p0, p3, p2);
        RegisterFace(faces, p0, p1, p3);
        RegisterFace(faces, p0, p2, p1);
    }

    surface.Triangles.reserve(faces.size());

    std::unordered_set<uint32_t> surfaceParticles;
    surfaceParticles.reserve(faces.size() * 2u);

    for (const auto& [key, record] : faces)
    {
        static_cast<void>(key);

        // 規則的な四面体メッシュでは内部Faceは必ず2回、外表面Faceは1回現れます。
        // 1回だけ現れるFaceのみSurfaceへ残します。
        if (record.OccurrenceCount != 1u)
        {
            continue;
        }

        surface.Triangles.push_back(record.Triangle);
        surfaceParticles.insert(record.Triangle.ParticleA);
        surfaceParticles.insert(record.Triangle.ParticleB);
        surfaceParticles.insert(record.Triangle.ParticleC);
    }

    surface.SurfaceParticleIndices.assign(surfaceParticles.begin(), surfaceParticles.end());

    // unordered_set由来の順序を固定し、同じJelly Topologyから常に同じMesh Vertex順を得られるようにします。
    // Renderer側のParticle -> Vertex対応表やSelf Testを安定させるための決定的順序です。
    std::sort(surface.SurfaceParticleIndices.begin(), surface.SurfaceParticleIndices.end());

    return surface;
}

} // namespace ph
} // namespace Raven
