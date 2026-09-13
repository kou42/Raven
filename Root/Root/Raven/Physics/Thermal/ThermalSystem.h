#pragma once

namespace Raven
{
class Scene;

namespace ph
{
class ThermalSystem
{
public:
    // ECSを正規データとしてThermalWorldの非所有Registryを再構築します。
    // Game Logic後、Fixed Step前に呼ぶことでComponent追加・削除・Entity破棄を安全に反映します。
    static void SynchronizeWorld(Scene& scene);
};

} // namespace ph
} // namespace Raven
