#include "Raven/Physics/SoftBody/SoftBodyCloth.h"

#include <algorithm>
#include <cassert>

namespace Raven
{
namespace ph
{

SoftBodyCloth SoftBodyClothBuilder::Build(SoftBodySolver& solver, const SoftBodyClothSettings& settings)
{
    assert(settings.Rows > 0u);
    assert(settings.Columns > 0u);

    SoftBodyCloth cloth{};
    cloth.Rows = settings.Rows;
    cloth.Columns = settings.Columns;

    const uint32_t vertexRows = settings.Rows + 1u;
    const uint32_t vertexColumns = settings.Columns + 1u;
    cloth.ParticleIndices.reserve(static_cast<size_t>(vertexRows) * static_cast<size_t>(vertexColumns));

    const float safeWidth = std::max(settings.Width, 0.0001f);
    const float safeHeight = std::max(settings.Height, 0.0001f);
    const float inverseMass = std::max(settings.InverseMass, 0.0f);

    // row=0がCloth上端になるよう、Yは+Height/2から-Height/2へ下げて生成します。
    // Mesh側のGrid頂点順もrow-majorなので、後でParticleと頂点を1対1対応させられます。
    for (uint32_t row = 0u; row < vertexRows; ++row)
    {
        const float v = static_cast<float>(row) / static_cast<float>(settings.Rows);
        const float y = (0.5f - v) * safeHeight;

        for (uint32_t column = 0u; column < vertexColumns; ++column)
        {
            const float u = static_cast<float>(column) / static_cast<float>(settings.Columns);
            const float x = (u - 0.5f) * safeWidth;

            float particleInverseMass = inverseMass;
            const bool isTopLeft = row == 0u && column == 0u;
            const bool isTopRight = row == 0u && column == settings.Columns;

            if ((settings.PinTopLeft && isTopLeft) || (settings.PinTopRight && isTopRight))
            {
                particleInverseMass = 0.0f;
            }

            const uint32_t particleIndex = solver.AddParticle(
                math::Vec3{ x, y, 0.0f },
                particleInverseMass);
            cloth.ParticleIndices.push_back(particleIndex);
        }
    }

    // Structural Constraint:
    // 水平・垂直の隣接Particleを接続し、布の縦横方向の伸びを抑えます。
    for (uint32_t row = 0u; row < vertexRows; ++row)
    {
        for (uint32_t column = 0u; column < vertexColumns; ++column)
        {
            if (column + 1u < vertexColumns)
            {
                solver.AddDistanceConstraint(
                    cloth.GetParticleIndex(row, column),
                    cloth.GetParticleIndex(row, column + 1u),
                    settings.StructuralCompliance);
            }

            if (row + 1u < vertexRows)
            {
                solver.AddDistanceConstraint(
                    cloth.GetParticleIndex(row, column),
                    cloth.GetParticleIndex(row + 1u, column),
                    settings.StructuralCompliance);
            }
        }
    }

    // Shear Constraint:
    // 各Quadの2本の対角線を接続します。これが無いと格子が菱形へ潰れやすいため、
    // Clothの面積感を保つための最小構成として最初から追加します。
    for (uint32_t row = 0u; row < settings.Rows; ++row)
    {
        for (uint32_t column = 0u; column < settings.Columns; ++column)
        {
            const uint32_t topLeft = cloth.GetParticleIndex(row, column);
            const uint32_t topRight = cloth.GetParticleIndex(row, column + 1u);
            const uint32_t bottomLeft = cloth.GetParticleIndex(row + 1u, column);
            const uint32_t bottomRight = cloth.GetParticleIndex(row + 1u, column + 1u);

            solver.AddDistanceConstraint(topLeft, bottomRight, settings.ShearCompliance);
            solver.AddDistanceConstraint(topRight, bottomLeft, settings.ShearCompliance);
        }
    }

    return cloth;
}

} // namespace ph
} // namespace Raven
