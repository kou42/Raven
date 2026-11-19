#pragma once

#include "Raven/Physics/Field/DenseSDF.h"
#include "Raven/Physics/Thermal/TemperatureField.h"

namespace Raven::ph
{

struct GridHeatParticle
{
    math::Vec3 Position{};
    double Watts = 0.0;
};

enum class GridHeatStatus { InvalidInput, UnstableStep, Advanced };

class GridTemperatureField final : public TemperatureField
{
public:
    // 容積熱容量 [J/(m³ K)] と熱伝導率 [W/(m K)] は格子内で一様とする。
    static std::optional<GridTemperatureField> Create(const DenseSDFGrid& grid,
        double volumetricHeatCapacity, double conductivity, std::span<const double> temperatureKelvin);

    float Evaluate(const math::Vec3& position) const override;
    float EvaluateInfluence(const math::Vec3& position) const override;

    // 速度は各セルの正軸面 [m/s]、体積熱源は [W/m³]。空の配列はゼロを意味する。
    // 外周面は断熱・不透過。失敗したStepでは温度を公開しない。
    GridHeatStatus Step(double dt, std::span<const math::Vec3> positiveFaceVelocity,
        std::span<const double> heatDensity, std::span<const GridHeatParticle> particles);

    std::optional<std::size_t> FindCell(const math::Vec3& position) const;
    const DenseSDFGrid& GetGrid() const;
    std::span<const double> GetTemperatures() const;
    double GetTotalEnergy() const;

private:
    DenseSDFGrid m_Grid{};
    double m_CellVolume = 0.0;
    double m_CellHeatCapacity = 0.0;
    double m_Conductivity = 0.0;
    std::vector<double> m_Temperature;
};

// 着火温度と燃料量だけを持つ独立の簡易セル反応モデル。隣接セルへは熱拡散で伝播する。
// 反応熱は試算値として返し、温度と燃料は呼び出し側が同じStepで確定する。
struct GridCombustionModel
{
    double IgnitionKelvin = 0.0;
    double ConsumptionPerSecond = 0.0; // kg/(m³ s)
    double HeatPerMass = 0.0; // J/kg

    bool Predict(std::span<const double> temperature, std::span<const double> fuel,
        double dt, std::vector<double>& remainingFuel, std::vector<double>& heatDensity) const;
};

} // namespace Raven::ph
