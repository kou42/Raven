#pragma once

#include "Raven/Physics/Thermal/GridTemperatureField.h"

#include <memory>

namespace Raven::ph
{
class GPUGridFluid;
struct ElectromagneticPotentialResult;

enum class GridThermalStepStatus { InvalidInput, WaitingForVelocity, UnstableStep, Advanced };

class GridThermalCoupling
{
public:
    explicit GridThermalCoupling(GridTemperatureField field);
    bool SetJouleHeating(std::shared_ptr<const ElectromagneticPotentialResult> result);
    void SetFluidVelocity(std::shared_ptr<const VectorField> field);
    void SetGPUFluid(const std::shared_ptr<GPUGridFluid>& fluid);
    void SetParticles(std::span<const GridHeatParticle> particles);
    bool SetCombustion(GridCombustionModel model, std::span<const double> fuelDensity);
    void Step(float fixedDeltaTime);

    TemperatureField& GetEnvironmentField();
    const GridTemperatureField& GetField() const;
    std::span<const double> GetFuelDensity() const;
    GridThermalStepStatus GetLastStatus() const;
    std::uint64_t GetUsedVelocityGeneration() const;

private:
    GridTemperatureField m_Field;
    std::shared_ptr<const ElectromagneticPotentialResult> m_Joule;
    std::shared_ptr<const VectorField> m_CPUVelocity;
    // Fluidの所有者がContextの寿命を管理する。WorldからGPU資源の寿命を延長しない。
    std::weak_ptr<GPUGridFluid> m_GPUFluid;
    bool m_UseGPU = false;
    std::vector<GridHeatParticle> m_Particles;
    std::optional<GridCombustionModel> m_Combustion;
    std::vector<double> m_Fuel;
    GridThermalStepStatus m_LastStatus = GridThermalStepStatus::InvalidInput;
    std::uint64_t m_UsedVelocityGeneration = 0;
};

} // namespace Raven::ph
