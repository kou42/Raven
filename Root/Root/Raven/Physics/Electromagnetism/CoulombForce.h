#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven::ph
{

struct CoulombForceSettings
{
    // 真空中のクーロン定数 [N m^2 / C^2]。
    double CoulombConstant = 8.9875517923e9;

    // 点電荷が完全に同位置へ重なった場合の特異点を避ける最小距離 [m]。
    // 現段階ではゲーム物理として安定に扱えるよう、距離をこの値で下限クランプします。
    float MinimumDistance = 1.0e-4f;
};

// sourcePositionにあるsourceChargeが、targetPositionにあるtargetChargeへ及ぼす力を返します。
// 戻り値はtargetへ加えるworld-space Force [N] です。
// 同符号ならsourceからtargetへ向かう斥力、異符号ならsourceへ向かう引力になります。
math::Vec3 ComputeCoulombForce(
    const math::Vec3& sourcePosition,
    double sourceChargeCoulombs,
    const math::Vec3& targetPosition,
    double targetChargeCoulombs,
    const CoulombForceSettings& settings = CoulombForceSettings{});

} // namespace Raven::ph
