#include "Raven/Physics/Coupling/GridThermalCoupling.h"

#include "Raven/Physics/Electromagnetism/GridElectromagnetism.h"
#include "Raven/Physics/Fluid/GPUGridFluid.h"

#include <utility>

namespace Raven::ph
{
namespace
{
bool SameGrid(const DenseSDFGrid& left, const DenseSDFGrid& right)
{
    return left.Origin == right.Origin && left.Spacing == right.Spacing && left.Size == right.Size;
}
}

GridThermalCoupling::GridThermalCoupling(GridTemperatureField field) : m_Field(std::move(field))
{
}

bool GridThermalCoupling::SetJouleHeating(std::shared_ptr<const ElectromagneticPotentialResult> result)
{
    // 同じセルの W/m³ を使い、別解像度の点サンプリングによる熱源総量の変化を防ぐ。
    if (result != nullptr && (SameGrid(result->Grid, m_Field.GetGrid()) == false ||
        result->JouleHeat.size() != m_Field.GetTemperatures().size()))
    {
        return false;
    }
    m_Joule = std::move(result);
    return true;
}

void GridThermalCoupling::SetFluidVelocity(std::shared_ptr<const VectorField> field)
{
    m_CPUVelocity = std::move(field);
    m_GPUFluid.reset();
    m_UseGPU = false;
}

void GridThermalCoupling::SetGPUFluid(const std::shared_ptr<GPUGridFluid>& fluid)
{
    m_GPUFluid = fluid;
    m_CPUVelocity.reset();
    m_UseGPU = fluid != nullptr;
}

void GridThermalCoupling::SetParticles(std::span<const GridHeatParticle> particles)
{
    m_Particles.assign(particles.begin(), particles.end());
}

bool GridThermalCoupling::SetCombustion(GridCombustionModel model, std::span<const double> fuelDensity)
{
    std::vector<double> remaining;
    std::vector<double> heat;
    if (model.Predict(m_Field.GetTemperatures(), fuelDensity, 1.0, remaining, heat) == false)
    {
        return false;
    }
    m_Fuel = std::vector<double>(fuelDensity.begin(), fuelDensity.end());
    m_Combustion = model;
    return true;
}

void GridThermalCoupling::Step(float fixedDeltaTime)
{
    m_LastStatus = GridThermalStepStatus::InvalidInput;
    const auto& grid = m_Field.GetGrid();
    const auto count = m_Field.GetTemperatures().size();
    std::span<const math::Vec3> gpuVelocity;
    std::uint64_t generation = 0;
    const auto gpu = m_GPUFluid.lock();
    if (m_UseGPU)
    {
        if (gpu == nullptr || SameGrid(gpu->GetVelocityGrid(), grid) == false ||
            gpu->UpdateVelocityReadback() == false)
        {
            return;
        }
        gpuVelocity = gpu->GetCompletedVelocities();
        if (gpuVelocity.empty())
        {
            m_LastStatus = GridThermalStepStatus::WaitingForVelocity;
            return;
        }
        generation = gpu->GetCompletedVelocityGeneration();
    }

    std::vector<math::Vec3> faces;
    if (gpuVelocity.empty() == false || m_CPUVelocity != nullptr)
    {
        faces.resize(count);
        const std::array<std::size_t, 3> stride{ 1, grid.Size[0],
            static_cast<std::size_t>(grid.Size[0]) * grid.Size[1] };
        for (std::size_t cell = 0; cell < count; ++cell)
        {
            const std::array coordinate{
                static_cast<std::uint32_t>(cell % grid.Size[0]),
                static_cast<std::uint32_t>((cell / stride[1]) % grid.Size[1]),
                static_cast<std::uint32_t>(cell / stride[2])
            };
            for (int axis = 0; axis < 3; ++axis)
            {
                if (coordinate[axis] + 1 == grid.Size[axis])
                {
                    continue;
                }
                if (m_CPUVelocity != nullptr)
                {
                    auto face = grid.GetCellCenter(coordinate[0], coordinate[1], coordinate[2]);
                    face[axis] += 0.5f * grid.Spacing[axis];
                    faces[cell][axis] = m_CPUVelocity->Evaluate(face)[axis];
                }
                else
                {
                    faces[cell][axis] = 0.5f * gpuVelocity[cell][axis] +
                        0.5f * gpuVelocity[cell + stride[axis]][axis];
                }
            }
        }
    }

    std::span<const double> heat;
    if (m_Joule != nullptr)
    {
        heat = m_Joule->JouleHeat;
    }
    std::vector<double> remaining;
    std::vector<double> reactionHeat;
    if (m_Combustion.has_value())
    {
        if (m_Combustion->Predict(m_Field.GetTemperatures(), m_Fuel,
            fixedDeltaTime, remaining, reactionHeat) == false)
        {
            return;
        }
        for (std::size_t cell = 0; cell < heat.size(); ++cell)
        {
            reactionHeat[cell] += heat[cell];
        }
        heat = reactionHeat;
    }

    switch (m_Field.Step(fixedDeltaTime, faces, heat, m_Particles))
    {
    case GridHeatStatus::Advanced:
        if (m_Combustion.has_value())
        {
            m_Fuel.swap(remaining);
        }
        m_UsedVelocityGeneration = generation;
        m_LastStatus = GridThermalStepStatus::Advanced;
        break;
    case GridHeatStatus::UnstableStep:
        m_LastStatus = GridThermalStepStatus::UnstableStep;
        break;
    case GridHeatStatus::InvalidInput:
        break;
    }
}

TemperatureField& GridThermalCoupling::GetEnvironmentField() { return m_Field; }
const GridTemperatureField& GridThermalCoupling::GetField() const { return m_Field; }
std::span<const double> GridThermalCoupling::GetFuelDensity() const { return m_Fuel; }
GridThermalStepStatus GridThermalCoupling::GetLastStatus() const { return m_LastStatus; }
std::uint64_t GridThermalCoupling::GetUsedVelocityGeneration() const { return m_UsedVelocityGeneration; }

} // namespace Raven::ph
