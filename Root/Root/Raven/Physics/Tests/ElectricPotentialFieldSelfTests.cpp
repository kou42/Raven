#include "Raven/Physics/Tests/ElectricPotentialFieldSelfTests.h"

#include <cassert>
#include <cmath>

#include "Raven/Physics/Electromagnetism/ElectricField.h"
#include "Raven/Physics/Electromagnetism/ElectricPotentialField.h"

namespace Raven::ph::tests
{
namespace
{
bool NearlyEqual(float left, float right, float epsilon = 1.0e-3f)
{
    return std::abs(left - right) <= epsilon;
}
}

void RunElectricPotentialFieldSelfTests()
{
    // 定電位は位置に依存せず、ScalarFieldとして常に同じ1値を返します。
    const UniformElectricPotentialField uniformPotential(12.5f);
    assert(NearlyEqual(uniformPotential.Evaluate({ 0.0f, 0.0f, 0.0f }), 12.5f));
    assert(NearlyEqual(uniformPotential.Evaluate({ 100.0f, -20.0f, 5.0f }), 12.5f));

    constexpr double microCoulomb = 1.0e-6;
    const PointChargeElectricPotentialField positivePotential(
        { 0.0f, 0.0f, 0.0f }, microCoulomb);
    const PointChargeElectricPotentialField negativePotential(
        { 0.0f, 0.0f, 0.0f }, -microCoulomb);

    const float potentialAtOneMeter = positivePotential.Evaluate({ 1.0f, 0.0f, 0.0f });
    const float potentialAtTwoMeters = positivePotential.Evaluate({ 2.0f, 0.0f, 0.0f });
    assert(potentialAtOneMeter > 0.0f);
    assert(NearlyEqual(potentialAtTwoMeters / potentialAtOneMeter, 0.5f));
    assert(negativePotential.Evaluate({ 1.0f, 0.0f, 0.0f }) < 0.0f);

    // 点電荷ではE=-grad(V)です。x軸上で中心差分した-dV/dxと既存ElectricFieldを比較し、
    // ScalarFieldとVectorFieldが同じ物理モデルを表していることを回帰テストします。
    const PointChargeElectricField electricField({ 0.0f, 0.0f, 0.0f }, microCoulomb);
    constexpr float sampleX = 2.0f;
    constexpr float epsilon = 0.001f;
    const float potentialPlus = positivePotential.Evaluate({ sampleX + epsilon, 0.0f, 0.0f });
    const float potentialMinus = positivePotential.Evaluate({ sampleX - epsilon, 0.0f, 0.0f });
    const float electricFieldFromPotential = -(potentialPlus - potentialMinus) / (2.0f * epsilon);
    const float electricFieldX = electricField.Evaluate({ sampleX, 0.0f, 0.0f }).x;
    assert(NearlyEqual(electricFieldFromPotential / electricFieldX, 1.0f, 2.0e-3f));

    // MinimumDistance内では1/rの発散をClampし、中心評価でも有限値を維持します。
    CoulombForceSettings softenedSettings{};
    softenedSettings.MinimumDistance = 0.5f;
    const PointChargeElectricPotentialField softenedPotential(
        { 0.0f, 0.0f, 0.0f }, microCoulomb, softenedSettings);
    const float potentialAtCenter = softenedPotential.Evaluate({ 0.0f, 0.0f, 0.0f });
    const float potentialInsideClamp = softenedPotential.Evaluate({ 0.25f, 0.0f, 0.0f });
    assert(std::isfinite(potentialAtCenter));
    assert(NearlyEqual(potentialAtCenter, potentialInsideClamp));
}

} // namespace Raven::ph::tests
