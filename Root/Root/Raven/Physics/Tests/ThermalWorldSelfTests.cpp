#include "Raven/Physics/Thermal/ThermalWorld.h"

#include <cassert>
#include <cmath>

namespace Raven::ph::tests
{
namespace
{
float CalculateThermalEnergy(const ThermalBody& body)
{
    return body.GetHeatCapacity() * body.Temperature;
}
}

// Debuggerや既存Self Test runnerから呼び出すための最小検証です。
// 373.15Kと293.15Kの同一熱容量Bodyを接触させ、熱が高温側から低温側へ移動し、
// 閉じた2 Body系の総熱エネルギーが保存されることを確認します。
void RunThermalWorldSelfTests()
{
    ThermalBody hotBody{};
    hotBody.Temperature = 373.15f;
    hotBody.Mass = 1.0f;
    hotBody.Material.SpecificHeatCapacity = 500.0f;
    hotBody.Material.ThermalConductivity = 50.0f;

    ThermalBody coldBody{};
    coldBody.Temperature = 293.15f;
    coldBody.Mass = 1.0f;
    coldBody.Material.SpecificHeatCapacity = 500.0f;
    coldBody.Material.ThermalConductivity = 50.0f;

    ThermalWorld world{};
    assert(world.RegisterBody(hotBody) == true);
    assert(world.RegisterBody(coldBody) == true);
    assert(world.RegisterBody(hotBody) == false);

    ThermalContact contact{};
    contact.BodyA = &hotBody;
    contact.BodyB = &coldBody;
    contact.ContactArea = 0.01f;
    contact.ConductionDistance = 0.01f;
    assert(world.RegisterContact(contact) == true);

    const float initialEnergy =
        CalculateThermalEnergy(hotBody) + CalculateThermalEnergy(coldBody);

    world.Step(1.0f);

    const float finalEnergy =
        CalculateThermalEnergy(hotBody) + CalculateThermalEnergy(coldBody);

    assert(hotBody.Temperature < 373.15f);
    assert(coldBody.Temperature > 293.15f);
    assert(std::abs(finalEnergy - initialEnergy) < 1.0f);

    // 十分大きいStepでも平衡温度を飛び越えないことを確認します。
    world.Step(100000.0f);
    assert(std::abs(hotBody.Temperature - 333.15f) < 1.0e-3f);
    assert(std::abs(coldBody.Temperature - 333.15f) < 1.0e-3f);
}

}
