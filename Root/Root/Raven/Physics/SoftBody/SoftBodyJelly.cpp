#include "Raven/Physics/SoftBody/SoftBodyJelly.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <unordered_set>

namespace Raven
{
namespace ph
{
namespace
{

uint64_t MakeParticlePairKey(uint32_t particleA, uint32_t particleB)
{
    const uint32_t lower = std::min(particleA, particleB);
    const uint32_t upper = std::max(particleA, particleB);
    return (static_cast<uint64_t>(lower) << 32u) | static_cast<uint64_t>(upper);
}

void AddUniqueDistanceConstraint(
    SoftBodySolver& solver,
    std::unordered_set<uint64_t>& registeredEdges,
    uint32_t particleA,
    uint32_t particleB,
    float compliance)
{
    const uint64_t key = MakeParticlePairKey(particleA, particleB);
    if (registeredEdges.find(key) != registeredEdges.end())
    {
        return;
    }

    registeredEdges.insert(key);
    solver.AddDistanceConstraint(particleA, particleB, compliance);
}

void AddTetrahedron(
    SoftBodySolver& solver,
    SoftBodyJelly& jelly,
    std::unordered_set<uint64_t>& registeredEdges,
    uint32_t particle0,
    uint32_t particle1,
    uint32_t particle2,
    uint32_t particle3,
    float distanceCompliance,
    float volumeCompliance)
{
    const SoftBodyTetrahedron tetrahedron{
        particle0,
        particle1,
        particle2,
        particle3
    };
    jelly.Tetrahedra.push_back(tetrahedron);

    // ========================================================================
    // Tetrahedron Volume Constraint
    // ========================================================================
    // RestVolumeはBuilder生成直後の未変形位置から計算します。
    // 符号付き体積を保持するため、後から四面体が反転した場合も元の向きへ戻すConstraint Errorを
    // 作れます。
    jelly.VolumeConstraints.push_back(CreateXPBDVolumeConstraint(
        solver.GetParticles(),
        particle0,
        particle1,
        particle2,
        particle3,
        volumeCompliance));

    // ========================================================================
    // Tetrahedron Edge Distance Constraints
    // ========================================================================
    // 四面体は6本のEdgeを持ちます。
    // 隣接Tetrahedron間ではEdgeが共有されるため、同じDistance Constraintを重複登録すると
    // そのEdgeだけ実質的に硬くなってMaterialが不均一になります。
    // そこでParticle Pairをglobalに重複排除して、各Edgeを1回だけSolverへ登録します。
    const std::array<uint32_t, 4u> particles{
        particle0,
        particle1,
        particle2,
        particle3
    };

    for (uint32_t i = 0u; i < 4u; ++i)
    {
        for (uint32_t j = i + 1u; j < 4u; ++j)
        {
            AddUniqueDistanceConstraint(
                solver,
                registeredEdges,
                particles[i],
                particles[j],
                distanceCompliance);
        }
    }
}

} // namespace

SoftBodyJelly SoftBodyJellyBuilder::Build(
    SoftBodySolver& solver,
    const SoftBodyJellySettings& settings)
{
    assert(settings.CellsX > 0u);
    assert(settings.CellsY > 0u);
    assert(settings.CellsZ > 0u);

    SoftBodyJelly jelly{};
    jelly.CellsX = settings.CellsX;
    jelly.CellsY = settings.CellsY;
    jelly.CellsZ = settings.CellsZ;
    jelly.VelocityDamping = std::clamp(settings.VelocityDamping, 0.0f, 1.0f);

    const uint32_t vertexCountX = settings.CellsX + 1u;
    const uint32_t vertexCountY = settings.CellsY + 1u;
    const uint32_t vertexCountZ = settings.CellsZ + 1u;

    const std::size_t particleCount =
        static_cast<std::size_t>(vertexCountX)
        * static_cast<std::size_t>(vertexCountY)
        * static_cast<std::size_t>(vertexCountZ);
    jelly.ParticleIndices.reserve(particleCount);

    const std::size_t tetrahedronCount =
        static_cast<std::size_t>(settings.CellsX)
        * static_cast<std::size_t>(settings.CellsY)
        * static_cast<std::size_t>(settings.CellsZ)
        * 6u;
    jelly.Tetrahedra.reserve(tetrahedronCount);
    jelly.VolumeConstraints.reserve(tetrahedronCount);

    const float safeWidth = std::max(settings.Width, 0.0001f);
    const float safeHeight = std::max(settings.Height, 0.0001f);
    const float safeDepth = std::max(settings.Depth, 0.0001f);
    const float inverseMass = std::max(settings.InverseMass, 0.0f);
    const float distanceCompliance = std::max(settings.DistanceCompliance, 0.0f);
    const float volumeCompliance = std::max(settings.VolumeCompliance, 0.0f);

    // ========================================================================
    // Regular 3D Particle Grid
    // ========================================================================
    // x/y/zをそれぞれ[-size/2, +size/2]へ均等配置します。
    // z-major -> y -> x の順で格納し、GetParticleIndex(x,y,z)と一致させます。
    for (uint32_t z = 0u; z < vertexCountZ; ++z)
    {
        const float w = static_cast<float>(z) / static_cast<float>(settings.CellsZ);
        const float positionZ = (w - 0.5f) * safeDepth;

        for (uint32_t y = 0u; y < vertexCountY; ++y)
        {
            const float v = static_cast<float>(y) / static_cast<float>(settings.CellsY);
            const float positionY = (v - 0.5f) * safeHeight;

            for (uint32_t x = 0u; x < vertexCountX; ++x)
            {
                const float u = static_cast<float>(x) / static_cast<float>(settings.CellsX);
                const float positionX = (u - 0.5f) * safeWidth;

                jelly.ParticleIndices.push_back(solver.AddParticle(
                    math::Vec3{ positionX, positionY, positionZ },
                    inverseMass));
            }
        }
    }

    // Tetrahedron間で共有されるEdgeのDistance Constraint重複を防ぎます。
    std::unordered_set<uint64_t> registeredEdges;
    registeredEdges.reserve(tetrahedronCount * 3u);

    // ========================================================================
    // Cube Cell -> 6 Tetrahedra
    // ========================================================================
    // 各Cellの8頂点をbit表現に対応させて次のように呼びます。
    //
    //   v000 = (x,   y,   z)
    //   v100 = (x+1, y,   z)
    //   v010 = (x,   y+1, z)
    //   v110 = (x+1, y+1, z)
    //   v001 = (x,   y,   z+1)
    //   v101 = (x+1, y,   z+1)
    //   v011 = (x,   y+1, z+1)
    //   v111 = (x+1, y+1, z+1)
    //
    // すべてのTetがv000-v111の体対角線を共有する6分割を使用します。
    // 同一規則を全Cellへ適用すると、隣接Cellが共有するQuad面のTriangle分割も一致します。
    for (uint32_t z = 0u; z < settings.CellsZ; ++z)
    {
        for (uint32_t y = 0u; y < settings.CellsY; ++y)
        {
            for (uint32_t x = 0u; x < settings.CellsX; ++x)
            {
                const uint32_t v000 = jelly.GetParticleIndex(x, y, z);
                const uint32_t v100 = jelly.GetParticleIndex(x + 1u, y, z);
                const uint32_t v010 = jelly.GetParticleIndex(x, y + 1u, z);
                const uint32_t v110 = jelly.GetParticleIndex(x + 1u, y + 1u, z);
                const uint32_t v001 = jelly.GetParticleIndex(x, y, z + 1u);
                const uint32_t v101 = jelly.GetParticleIndex(x + 1u, y, z + 1u);
                const uint32_t v011 = jelly.GetParticleIndex(x, y + 1u, z + 1u);
                const uint32_t v111 = jelly.GetParticleIndex(x + 1u, y + 1u, z + 1u);

                AddTetrahedron(
                    solver, jelly, registeredEdges,
                    v000, v100, v110, v111,
                    distanceCompliance, volumeCompliance);
                AddTetrahedron(
                    solver, jelly, registeredEdges,
                    v000, v110, v010, v111,
                    distanceCompliance, volumeCompliance);
                AddTetrahedron(
                    solver, jelly, registeredEdges,
                    v000, v010, v011, v111,
                    distanceCompliance, volumeCompliance);
                AddTetrahedron(
                    solver, jelly, registeredEdges,
                    v000, v011, v001, v111,
                    distanceCompliance, volumeCompliance);
                AddTetrahedron(
                    solver, jelly, registeredEdges,
                    v000, v001, v101, v111,
                    distanceCompliance, volumeCompliance);
                AddTetrahedron(
                    solver, jelly, registeredEdges,
                    v000, v101, v100, v111,
                    distanceCompliance, volumeCompliance);
            }
        }
    }

    return jelly;
}

void StepSoftBodyJelly(SoftBodySolver& solver, SoftBodyJelly& jelly, float deltaTime)
{
    if (deltaTime <= 0.0f)
    {
        return;
    }

    solver.StepWithVolumeConstraints(deltaTime, jelly.VolumeConstraints);

    // ========================================================================
    // Time-step Corrected Velocity Damping
    // ========================================================================
    // Velocity *= (1 - damping) を毎Stepそのまま掛けると、30Hzと120Hzで1秒後の減衰量が
    // 大きく変わります。そこでVelocityDampingを「60Hzの1frameで失う割合」と解釈し、
    //
    //   retention = pow(1 - damping, deltaTime * 60)
    //
    // として時間刻みを補正します。
    // damping=0ならretention=1、damping=1ならretention=0です。
    const float damping = std::clamp(jelly.VelocityDamping, 0.0f, 1.0f);
    float retention = 1.0f;
    if (damping >= 1.0f)
    {
        retention = 0.0f;
    }
    else if (damping > 0.0f)
    {
        retention = std::pow(1.0f - damping, deltaTime * 60.0f);
    }

    std::vector<SoftBodyParticle>& particles = solver.GetParticles();
    for (uint32_t particleIndex : jelly.ParticleIndices)
    {
        if (particleIndex >= particles.size())
        {
            continue;
        }

        SoftBodyParticle& particle = particles[particleIndex];
        if (particle.IsFixed())
        {
            particle.Velocity = math::Vec3{};
            continue;
        }

        particle.Velocity *= retention;
    }
}

} // namespace ph
} // namespace Raven
