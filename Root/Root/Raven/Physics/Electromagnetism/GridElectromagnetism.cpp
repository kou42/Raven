#include "Raven/Physics/Electromagnetism/GridElectromagnetism.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace Raven::ph
{
namespace
{
bool Finite(const math::Vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

struct CellTopology
{
    explicit CellTopology(const DenseSDFGrid& grid)
        : Size{ grid.Size[0], grid.Size[1], grid.Size[2] },
          Stride{ 1, Size[0], Size[0] * Size[1] }
    {
    }

    std::size_t Coordinate(std::size_t cell, int axis) const
    {
        return cell / Stride[axis] % Size[axis];
    }

    std::array<std::size_t, 3> Size;
    std::array<std::size_t, 3> Stride;
};

struct Stencil
{
    std::array<double, 3> Positive{};
    double Diagonal = 0.0;
};

} // namespace

std::optional<ElectromagneticMaterialGrid> ElectromagneticMaterialGrid::Build(const DenseSDFGrid& grid,
    std::span<const ElectromagneticMaterial> materials, std::uint32_t backgroundID,
    std::span<const ElectromagneticMaterialRegion> regions)
{
    const auto count = grid.GetVoxelCount();
    if (count == 0 || backgroundID >= materials.size())
    {
        return std::nullopt;
    }

    for (const auto& material : materials)
    {
        if (std::isfinite(material.Conductivity) == false || material.Conductivity < 0.0 ||
            std::isfinite(material.Permeability) == false || material.Permeability <= 0.0 ||
            std::isfinite(material.Permittivity) == false || material.Permittivity <= 0.0)
        {
            return std::nullopt;
        }
    }
    for (const auto& region : regions)
    {
        if (region.MaterialID >= materials.size())
        {
            return std::nullopt;
        }
    }

    ElectromagneticMaterialGrid result{ grid, { materials.begin(), materials.end() },
        std::vector<std::uint32_t>(count, backgroundID) };
    const CellTopology topology(grid);
    for (std::size_t cell = 0; cell < count; ++cell)
    {
        const auto center = grid.GetCellCenter(static_cast<std::uint32_t>(topology.Coordinate(cell, 0)),
            static_cast<std::uint32_t>(topology.Coordinate(cell, 1)),
            static_cast<std::uint32_t>(topology.Coordinate(cell, 2)));
        for (const auto& region : regions)
        {
            const float distance = region.SignedDistance.Evaluate(center);
            if (std::isfinite(distance) == false)
            {
                return std::nullopt;
            }
            if (distance <= 0.0f)
            {
                result.MaterialIDs[cell] = region.MaterialID;
                break;
            }
        }
    }
    return result;
}

std::optional<std::size_t> ElectromagneticPotentialResult::FindCell(const math::Vec3& position) const
{
    if (Finite(position) == false || Grid.GetVoxelCount() == 0)
    {
        return std::nullopt;
    }
    const CellTopology topology(Grid);
    std::size_t index = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double coordinate = (static_cast<double>(position[axis]) - Grid.Origin[axis]) / Grid.Spacing[axis];
        if (coordinate < 0.0 || coordinate >= static_cast<double>(topology.Size[axis]))
        {
            return std::nullopt;
        }
        index += static_cast<std::size_t>(coordinate) * topology.Stride[axis];
    }
    return index;
}

ElectromagneticSolveStatus SolveElectromagneticPotential(const ElectromagneticMaterialGrid& material,
    ElectromagneticPotentialMode mode, std::span<const std::optional<double>> fixedPotential,
    std::span<const double> chargeDensity, std::uint32_t maxIterations, double relativeTolerance,
    std::shared_ptr<const ElectromagneticPotentialResult>& result)
{
    using Status = ElectromagneticSolveStatus;
    result.reset();
    const auto& grid = material.Grid;
    const auto count = grid.GetVoxelCount();
    const bool current = mode == ElectromagneticPotentialMode::SteadyCurrent;
    if (current == false && mode != ElectromagneticPotentialMode::Electrostatic)
    {
        return Status::InvalidInput;
    }
    if (count == 0 || count != material.MaterialIDs.size() || fixedPotential.size() != count ||
        (chargeDensity.empty() == false && (current || chargeDensity.size() != count)) ||
        maxIterations == 0 || std::isfinite(relativeTolerance) == false ||
        relativeTolerance <= 0.0 || relativeTolerance >= 1.0)
    {
        return Status::InvalidInput;
    }

    auto solution = std::make_shared<ElectromagneticPotentialResult>();
    solution->Grid = grid;
    auto& potential = solution->Potential;
    potential.resize(count);
    std::vector<double> coefficient(count);
    std::vector<double> source(count);
    std::vector<Stencil> stencil(count);
    const double volume = static_cast<double>(grid.Spacing.x) * grid.Spacing.y * grid.Spacing.z;
    std::vector<std::size_t> reachable;
    std::vector<bool> referenced(count);
    for (std::size_t cell = 0; cell < count; ++cell)
    {
        const auto id = material.MaterialIDs[cell];
        if (id >= material.Materials.size())
        {
            return Status::InvalidInput;
        }
        coefficient[cell] = current ? material.Materials[id].Conductivity : material.Materials[id].Permittivity;
        if (std::isfinite(coefficient[cell]) == false || coefficient[cell] < 0.0 ||
            (current == false && coefficient[cell] == 0.0))
        {
            return Status::InvalidInput;
        }
        if (chargeDensity.empty() == false)
        {
            source[cell] = volume * chargeDensity[cell];
        }
        if (fixedPotential[cell].has_value())
        {
            potential[cell] = *fixedPotential[cell];
            if (coefficient[cell] == 0.0 || std::isfinite(potential[cell]) == false ||
                std::abs(potential[cell]) > std::numeric_limits<float>::max() || source[cell] != 0.0)
            {
                return Status::InvalidInput;
            }
            reachable.push_back(cell);
            referenced[cell] = true;
        }
        if (std::isfinite(source[cell]) == false)
        {
            return Status::InvalidInput;
        }
    }

    const CellTopology topology(grid);
    for (std::size_t cell = 0; cell < count; ++cell)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            if (topology.Coordinate(cell, axis) + 1 == topology.Size[axis])
            {
                continue;
            }
            const std::size_t neighbor = cell + topology.Stride[axis];
            const double lower = std::min(coefficient[cell], coefficient[neighbor]);
            const double upper = std::max(coefficient[cell], coefficient[neighbor]);
            if (lower == 0.0)
            {
                continue;
            }
            // 直列の半セル抵抗を面ごとに合成し、両側のセルが同じ流束を逆符号で使う。
            const double spacing = grid.Spacing[axis];
            const double weight = (lower / (0.5 + 0.5 * lower / upper)) * (volume / (spacing * spacing));
            if (std::isfinite(weight) == false || weight == 0.0)
            {
                return Status::InvalidInput;
            }
            stencil[cell].Positive[axis] = weight;
            stencil[cell].Diagonal += weight;
            stencil[neighbor].Diagonal += weight;
        }
    }

    const auto visitNeighbors = [&](std::size_t cell, const auto& visit)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            const auto step = topology.Stride[axis];
            if (stencil[cell].Positive[axis] > 0.0)
            {
                visit(cell + step, stencil[cell].Positive[axis]);
            }
            if (topology.Coordinate(cell, axis) > 0 && stencil[cell - step].Positive[axis] > 0.0)
            {
                visit(cell - step, stencil[cell - step].Positive[axis]);
            }
        }
    };

    // 絶縁体で分断された導体にも個別の電位基準が必要。反復を始める前に検出する。
    for (std::size_t head = 0; head < reachable.size(); ++head)
    {
        visitNeighbors(reachable[head], [&](std::size_t neighbor, double)
        {
            if (referenced[neighbor] == false)
            {
                referenced[neighbor] = true;
                reachable.push_back(neighbor);
            }
        });
    }

    std::vector<std::size_t> freeCells;
    double scale = 0.0;
    for (std::size_t cell = 0; cell < count; ++cell)
    {
        if (coefficient[cell] == 0.0 || fixedPotential[cell].has_value())
        {
            continue;
        }
        if (referenced[cell] == false)
        {
            return Status::MissingReference;
        }
        freeCells.push_back(cell);
        double cellScale = std::abs(source[cell]);
        visitNeighbors(cell, [&](std::size_t neighbor, double weight)
        {
            cellScale += weight * std::abs(potential[neighbor]);
        });
        if (std::isfinite(cellScale) == false || std::isfinite(stencil[cell].Diagonal) == false)
        {
            return Status::InvalidInput;
        }
        scale = std::max(scale, cellScale);
    }

    const auto residual = [&](std::size_t cell)
    {
        double value = source[cell];
        visitNeighbors(cell, [&](std::size_t neighbor, double weight)
        {
            value += weight * (potential[neighbor] - potential[cell]);
        });
        return value;
    };
    bool converged = scale == 0.0;
    for (std::uint32_t iteration = 0; iteration < maxIterations && converged == false; ++iteration)
    {
        for (const auto cell : freeCells)
        {
            potential[cell] += residual(cell) / stencil[cell].Diagonal;
        }
        converged = std::all_of(freeCells.begin(), freeCells.end(), [&](std::size_t cell)
        {
            return std::abs(residual(cell)) <= relativeTolerance * scale;
        });
    }
    if (converged == false)
    {
        return Status::IterationLimit;
    }

    solution->Electric.resize(count);
    solution->Current.resize(count);
    solution->JouleHeat.resize(count);
    solution->PositiveFaceFlux.resize(count);
    solution->NetOutwardFlux.resize(count);
    for (std::size_t cell = 0; cell < count; ++cell)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            const auto step = topology.Stride[axis];
            const double weight = stencil[cell].Positive[axis];
            if (weight > 0.0)
            {
                const double flux = weight * (potential[cell] - potential[cell + step]);
                solution->PositiveFaceFlux[cell][axis] = flux;
                solution->NetOutwardFlux[cell] += flux;
                solution->NetOutwardFlux[cell + step] -= flux;
            }
            if (coefficient[cell] == 0.0)
            {
                continue;
            }
            const double previous = topology.Coordinate(cell, axis) > 0
                ? solution->PositiveFaceFlux[cell - step][axis] : 0.0;
            const double next = solution->PositiveFaceFlux[cell][axis];
            // セル中心へ両面の密度を平均する。保存則の検証には面積分値を使う。
            const double density = 0.5 * (previous + next) * (grid.Spacing[axis] / volume);
            const double electric = density / coefficient[cell];
            if (std::isfinite(electric) == false || std::abs(electric) > std::numeric_limits<float>::max() ||
                (current && std::abs(density) > std::numeric_limits<float>::max()))
            {
                return Status::InvalidInput;
            }
            solution->Electric[cell][axis] = static_cast<float>(electric);
            if (current)
            {
                solution->Current[cell][axis] = static_cast<float>(density);
            }
        }
        solution->JouleHeat[cell] = math::Vec3::Dot(solution->Current[cell], solution->Electric[cell]);
        if (Finite(solution->Electric[cell]) == false || Finite(solution->Current[cell]) == false ||
            std::isfinite(solution->JouleHeat[cell]) == false ||
            std::abs(potential[cell]) > std::numeric_limits<float>::max())
        {
            return Status::InvalidInput;
        }
    }
    result = std::move(solution);
    return Status::Converged;
}

GridElectricPotentialField::GridElectricPotentialField(std::shared_ptr<const ElectromagneticPotentialResult> result)
    : m_Result(std::move(result))
{
}

float GridElectricPotentialField::Evaluate(const math::Vec3& position) const
{
    const auto cell = m_Result ? m_Result->FindCell(position) : std::nullopt;
    return cell && *cell < m_Result->Potential.size() ? static_cast<float>(m_Result->Potential[*cell]) : 0.0f;
}

GridElectricField::GridElectricField(std::shared_ptr<const ElectromagneticPotentialResult> result)
    : m_Result(std::move(result))
{
}

math::Vec3 GridElectricField::Evaluate(const math::Vec3& position) const
{
    const auto cell = m_Result ? m_Result->FindCell(position) : std::nullopt;
    return cell && *cell < m_Result->Electric.size() ? m_Result->Electric[*cell] : math::Vec3{};
}

GridCurrentDensityField::GridCurrentDensityField(std::shared_ptr<const ElectromagneticPotentialResult> result)
    : m_Result(std::move(result))
{
}

math::Vec3 GridCurrentDensityField::Evaluate(const math::Vec3& position) const
{
    const auto cell = m_Result ? m_Result->FindCell(position) : std::nullopt;
    return cell && *cell < m_Result->Current.size() ? m_Result->Current[*cell] : math::Vec3{};
}

GridJouleHeatField::GridJouleHeatField(std::shared_ptr<const ElectromagneticPotentialResult> result)
    : m_Result(std::move(result))
{
}

float GridJouleHeatField::Evaluate(const math::Vec3& position) const
{
    const auto cell = m_Result ? m_Result->FindCell(position) : std::nullopt;
    return cell && *cell < m_Result->JouleHeat.size() ? static_cast<float>(m_Result->JouleHeat[*cell]) : 0.0f;
}

std::optional<CoilMagneticField> CoilMagneticField::Create(std::span<const math::Vec3> loopVertices,
    double current, double permeability, double coreRadius)
{
    if (loopVertices.size() < 3 || std::isfinite(current) == false ||
        std::isfinite(permeability) == false || permeability <= 0.0 ||
        std::isfinite(coreRadius * coreRadius) == false || coreRadius <= 0.0 || coreRadius * coreRadius == 0.0 ||
        std::all_of(loopVertices.begin(), loopVertices.end(), Finite) == false)
    {
        return std::nullopt;
    }
    CoilMagneticField field;
    field.m_Coefficient = (permeability / (4.0 * std::numbers::pi)) * current;
    if (std::isfinite(field.m_Coefficient) == false)
    {
        return std::nullopt;
    }
    field.m_CoreRadiusSquared = coreRadius * coreRadius;
    field.m_Vertices.assign(loopVertices.begin(), loopVertices.end());
    return field;
}

math::Vec3 CoilMagneticField::Evaluate(const math::Vec3& position) const
{
    if (Finite(position) == false)
    {
        return {};
    }
    std::array<double, 3> total{};
    for (std::size_t i = 0; i < m_Vertices.size(); ++i)
    {
        const auto& start = m_Vertices[i];
        const auto& end = m_Vertices[(i + 1) % m_Vertices.size()];
        std::array<double, 3> segment{};
        std::array<double, 3> offset{};
        double lengthSquared = 0.0;
        double dot = 0.0;
        for (int axis = 0; axis < 3; ++axis)
        {
            segment[axis] = static_cast<double>(end[axis]) - start[axis];
            offset[axis] = static_cast<double>(position[axis]) - start[axis];
            lengthSquared += segment[axis] * segment[axis];
            dot += segment[axis] * offset[axis];
        }
        if (lengthSquared == 0.0)
        {
            continue;
        }
        const double length = std::sqrt(lengthSquared);
        const double along = dot / length;
        const std::array cross{
            segment[1] * offset[2] - segment[2] * offset[1],
            segment[2] * offset[0] - segment[0] * offset[2],
            segment[0] * offset[1] - segment[1] * offset[0]
        };
        const double radialSquared = (cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2])
            / lengthSquared + m_CoreRadiusSquared;
        const double nearEnd = along - length;
        // Biot–Savart を各直線区間で解析積分する。有限コアで導線上の特異性を抑える。
        const double integral = (along / std::hypot(along, std::sqrt(radialSquared))
            - nearEnd / std::hypot(nearEnd, std::sqrt(radialSquared))) / (radialSquared * length);
        for (int axis = 0; axis < 3; ++axis)
        {
            total[axis] += m_Coefficient * cross[axis] * integral;
        }
    }
    const math::Vec3 field{ static_cast<float>(total[0]), static_cast<float>(total[1]), static_cast<float>(total[2]) };
    return Finite(field) ? field : math::Vec3{};
}

} // namespace Raven::ph
