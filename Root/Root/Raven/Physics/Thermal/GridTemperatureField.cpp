#include "Raven/Physics/Thermal/GridTemperatureField.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace Raven::ph
{
namespace
{
bool Nonnegative(double value)
{
    return std::isfinite(value) && value >= 0.0;
}
}

std::optional<GridTemperatureField> GridTemperatureField::Create(const DenseSDFGrid& grid,
    double volumetricHeatCapacity, double conductivity, std::span<const double> temperatureKelvin)
{
    const auto count = grid.GetVoxelCount();
    const double volume = static_cast<double>(grid.Spacing.x) * grid.Spacing.y * grid.Spacing.z;
    const double capacity = volume * volumetricHeatCapacity;
    if (count == 0 || count != temperatureKelvin.size() || Nonnegative(capacity) == false || capacity == 0.0 ||
        Nonnegative(conductivity) == false)
    {
        return std::nullopt;
    }
    for (const double temperature : temperatureKelvin)
    {
        if (Nonnegative(temperature) == false || temperature > std::numeric_limits<float>::max() ||
            std::isfinite(temperature * capacity) == false)
        {
            return std::nullopt;
        }
    }

    GridTemperatureField field;
    field.m_Grid = grid;
    field.m_CellVolume = volume;
    field.m_CellHeatCapacity = capacity;
    field.m_Conductivity = conductivity;
    field.m_Temperature.assign(temperatureKelvin.begin(), temperatureKelvin.end());
    return field;
}

std::optional<std::size_t> GridTemperatureField::FindCell(const math::Vec3& position) const
{
    if (m_Temperature.empty())
    {
        return std::nullopt;
    }
    std::size_t index = 0;
    std::size_t stride = 1;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double cell = (static_cast<double>(position[axis]) - m_Grid.Origin[axis]) / m_Grid.Spacing[axis];
        if (Nonnegative(cell) == false || cell >= m_Grid.Size[axis])
        {
            return std::nullopt;
        }
        index += static_cast<std::size_t>(cell) * stride;
        stride *= m_Grid.Size[axis];
    }
    return index;
}

float GridTemperatureField::Evaluate(const math::Vec3& position) const
{
    const auto index = FindCell(position);
    return index.has_value() ? static_cast<float>(m_Temperature[*index]) : 0.0f;
}

float GridTemperatureField::EvaluateInfluence(const math::Vec3& position) const
{
    return FindCell(position).has_value() ? 1.0f : 0.0f;
}

const DenseSDFGrid& GridTemperatureField::GetGrid() const { return m_Grid; }
std::span<const double> GridTemperatureField::GetTemperatures() const { return m_Temperature; }
double GridTemperatureField::GetTotalEnergy() const
{
    return std::accumulate(m_Temperature.begin(), m_Temperature.end(), 0.0) * m_CellHeatCapacity;
}

GridHeatStatus GridTemperatureField::Step(double dt, std::span<const math::Vec3> positiveFaceVelocity,
    std::span<const double> heatDensity, std::span<const GridHeatParticle> particles)
{
    const std::size_t count = m_Temperature.size();
    if (count == 0 || Nonnegative(dt) == false || dt == 0.0 ||
        (positiveFaceVelocity.empty() == false && positiveFaceVelocity.size() != count) ||
        (heatDensity.empty() == false && heatDensity.size() != count))
    {
        return GridHeatStatus::InvalidInput;
    }

    const std::array<std::size_t, 3> stride{ 1, m_Grid.Size[0],
        static_cast<std::size_t>(m_Grid.Size[0]) * m_Grid.Size[1] };
    std::vector<double> change(count);
    std::vector<double> outgoing(count);
    for (std::size_t cell = 0; cell < count; ++cell)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            if ((cell / stride[axis]) % m_Grid.Size[axis] + 1 == m_Grid.Size[axis])
            {
                continue;
            }
            const auto neighbor = cell + stride[axis];
            const double spacing = m_Grid.Spacing[axis];
            const double diffusion = dt * (m_Conductivity * (m_CellVolume / spacing)) / (spacing * m_CellHeatCapacity);
            const double speed = positiveFaceVelocity.empty() ? 0.0 : positiveFaceVelocity[cell][axis];
            if (std::isfinite(speed) == false || Nonnegative(diffusion) == false)
            {
                return GridHeatStatus::InvalidInput;
            }
            const double flow = dt * speed / spacing;
            if (std::isfinite(flow) == false)
            {
                return GridHeatStatus::UnstableStep;
            }
            const double fromCell = diffusion + std::max(0.0, flow);
            const double fromNeighbor = diffusion + std::max(0.0, -flow);
            outgoing[cell] += fromCell;
            outgoing[neighbor] += fromNeighbor;
            // 同じ面を通る熱量を両セルへ逆符号で適用し、閉領域の総熱量を保存する。
            const double transfer = fromCell * m_Temperature[cell] - fromNeighbor * m_Temperature[neighbor];
            change[cell] -= transfer;
            change[neighbor] += transfer;
        }

        if (heatDensity.empty() == false)
        {
            if (Nonnegative(heatDensity[cell]) == false)
            {
                return GridHeatStatus::InvalidInput;
            }
            change[cell] += dt * heatDensity[cell] * (m_CellVolume / m_CellHeatCapacity);
        }
    }

    for (const auto& particle : particles)
    {
        if (Nonnegative(particle.Watts) == false)
        {
            return GridHeatStatus::InvalidInput;
        }
        const auto cell = FindCell(particle.Position);
        if (cell.has_value())
        {
            change[*cell] += dt * particle.Watts / m_CellHeatCapacity;
        }
    }
    for (std::size_t cell = 0; cell < count; ++cell)
    {
        if (outgoing[cell] > 1.0)
        {
            return GridHeatStatus::UnstableStep;
        }
        change[cell] += m_Temperature[cell];
        // 差し引きの丸め誤差だけで絶対零度を下回ることを防ぐ。
        if (change[cell] < 0.0 && change[cell] >= -4.0 * std::numeric_limits<double>::epsilon() * m_Temperature[cell])
        {
            change[cell] = 0.0;
        }
        if (Nonnegative(change[cell]) == false || change[cell] > std::numeric_limits<float>::max() ||
            std::isfinite(change[cell] * m_CellHeatCapacity) == false)
        {
            return GridHeatStatus::InvalidInput;
        }
    }
    m_Temperature.swap(change);
    return GridHeatStatus::Advanced;
}

bool GridCombustionModel::Predict(std::span<const double> temperature, std::span<const double> fuel,
    double dt, std::vector<double>& remainingFuel, std::vector<double>& heatDensity) const
{
    if (&remainingFuel == &heatDensity || temperature.size() != fuel.size() || Nonnegative(dt) == false ||
        dt == 0.0 || Nonnegative(IgnitionKelvin) == false || Nonnegative(ConsumptionPerSecond) == false ||
        Nonnegative(HeatPerMass) == false)
    {
        return false;
    }
    std::vector<double> remaining(fuel.size());
    std::vector<double> heat(fuel.size());
    for (std::size_t cell = 0; cell < fuel.size(); ++cell)
    {
        if (Nonnegative(fuel[cell]) == false || Nonnegative(temperature[cell]) == false)
        {
            return false;
        }
        const double consumed = temperature[cell] >= IgnitionKelvin
            ? std::min(fuel[cell], dt * ConsumptionPerSecond) : 0.0;
        remaining[cell] = fuel[cell] - consumed;
        heat[cell] = (consumed / dt) * HeatPerMass;
        if (Nonnegative(heat[cell]) == false)
        {
            return false;
        }
    }
    remainingFuel = std::move(remaining);
    heatDensity = std::move(heat);
    return true;
}

} // namespace Raven::ph
