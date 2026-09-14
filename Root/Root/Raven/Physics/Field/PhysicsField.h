#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven::ph
{

// Raven Physics内で扱う空間上の場を表す共通タグ基底です。
// Fieldごとに返す物理量の種類は異なるため、評価関数は値型別の派生境界へ分離します。
class PhysicsField
{
public:
    virtual ~PhysicsField() = default;
};

// world-space位置から3次元ベクトル値を評価する物理場の共通境界です。
// Electric / Gravity / Magnetic Fieldなどで利用します。
class VectorField : public PhysicsField
{
public:
    ~VectorField() override = default;

    virtual math::Vec3 Evaluate(const math::Vec3& worldPosition) const = 0;
};

// world-space位置からスカラー値を評価する物理場の共通境界です。
// 将来のTemperature / Pressure / ElectricPotentialなどで利用します。
class ScalarField : public PhysicsField
{
public:
    ~ScalarField() override = default;

    virtual float Evaluate(const math::Vec3& worldPosition) const = 0;
};

} // namespace Raven::ph
