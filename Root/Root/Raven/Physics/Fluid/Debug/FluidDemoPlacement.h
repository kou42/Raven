#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven::ph::debug
{
// Terrainが存在する原点周辺を避けつつ、Characterから確認しやすい距離へFluidデモを配置します。
// SimulationとDebug Teleportが同じ基準値を参照し、デモ位置変更時の座標ずれを防ぎます。
inline constexpr math::Vec3 FluidDemoCenter{ 50.0f, 4.0f, 50.0f };
inline constexpr math::Vec3 FluidDemoCharacterPosition{ 50.0f, 0.0f, 58.0f };
} // namespace Raven::ph::debug
