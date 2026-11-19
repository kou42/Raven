#pragma once

#include "Raven/Physics/Electromagnetism/ElectricField.h"
#include "Raven/Physics/Electromagnetism/ElectricPotentialField.h"
#include "Raven/Physics/Electromagnetism/MagneticField.h"
#include "Raven/Physics/Field/DenseSDF.h"

#include <memory>

namespace Raven::ph
{

struct ElectromagneticMaterial
{
    double Conductivity = 0.0;                 // S/m
    double Permeability = 1.25663706212e-6;     // H/m
    double Permittivity = 8.8541878128e-12;     // F/m
};

struct ElectromagneticMaterialRegion
{
    std::uint32_t MaterialID;
    const ScalarField& SignedDistance;
};

struct ElectromagneticMaterialGrid
{
    DenseSDFGrid Grid;
    std::vector<ElectromagneticMaterial> Materials;
    std::vector<std::uint32_t> MaterialIDs;

    // 最初に内側と判定した領域を採用し、領域外では背景IDを使う。入力の SDF は構築時のみ参照する。
    static std::optional<ElectromagneticMaterialGrid> Build(const DenseSDFGrid& grid,
        std::span<const ElectromagneticMaterial> materials, std::uint32_t backgroundID,
        std::span<const ElectromagneticMaterialRegion> regions);
};

enum class ElectromagneticPotentialMode { Electrostatic, SteadyCurrent };
enum class ElectromagneticSolveStatus { InvalidInput, MissingReference, IterationLimit, Converged };

struct ElectromagneticPotentialResult
{
    DenseSDFGrid Grid;
    std::vector<double> Potential;
    std::vector<math::Vec3> Electric;
    std::vector<math::Vec3> Current;
    std::vector<double> JouleHeat;
    // 正軸を向く各セル面の積分流束。静電では C、定常電流では A。
    std::vector<std::array<double, 3>> PositiveFaceFlux;
    // 外向き流束の合計。固定電位セルでは電極が供給する電荷または電流。
    std::vector<double> NetOutwardFlux;

    std::optional<std::size_t> FindCell(const math::Vec3& worldPosition) const;
};

// 固定電位はセル中心で指定する。空の値は自由セル。格子外面は同次 Neumann 境界。
// 静電の source は C/m³。定常電流では source を空とし、電極からのみ給電する。
ElectromagneticSolveStatus SolveElectromagneticPotential(const ElectromagneticMaterialGrid& material,
    ElectromagneticPotentialMode mode, std::span<const std::optional<double>> fixedPotential,
    std::span<const double> chargeDensity, std::uint32_t maxIterations, double relativeTolerance,
    std::shared_ptr<const ElectromagneticPotentialResult>& result);

class GridElectricPotentialField final : public ElectricPotentialField
{
public:
    explicit GridElectricPotentialField(std::shared_ptr<const ElectromagneticPotentialResult> result);
    float Evaluate(const math::Vec3& position) const override;
private:
    std::shared_ptr<const ElectromagneticPotentialResult> m_Result;
};

class GridElectricField final : public ElectricField
{
public:
    explicit GridElectricField(std::shared_ptr<const ElectromagneticPotentialResult> result);
    math::Vec3 Evaluate(const math::Vec3& position) const override;
private:
    std::shared_ptr<const ElectromagneticPotentialResult> m_Result;
};

class GridCurrentDensityField final : public VectorField
{
public:
    explicit GridCurrentDensityField(std::shared_ptr<const ElectromagneticPotentialResult> result);
    math::Vec3 Evaluate(const math::Vec3& position) const override;
private:
    std::shared_ptr<const ElectromagneticPotentialResult> m_Result;
};

class GridJouleHeatField final : public ScalarField
{
public:
    explicit GridJouleHeatField(std::shared_ptr<const ElectromagneticPotentialResult> result);
    float Evaluate(const math::Vec3& position) const override;
private:
    std::shared_ptr<const ElectromagneticPotentialResult> m_Result;
};

// 均一透磁率の無限領域を仮定し、閉じた折れ線コイルの定常電流を積分する。
class CoilMagneticField final : public MagneticField
{
public:
    static std::optional<CoilMagneticField> Create(std::span<const math::Vec3> loopVertices,
        double current, double permeability, double coreRadius);
    math::Vec3 Evaluate(const math::Vec3& position) const override;

private:
    std::vector<math::Vec3> m_Vertices;
    double m_Coefficient = 0.0;
    double m_CoreRadiusSquared = 0.0;
};

} // namespace Raven::ph
