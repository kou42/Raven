#pragma once

#include <vector>

#include "Raven/Physics/Contact.h"

namespace Raven
{
class Scene;

namespace ph
{
class ThermalSystem
{
public:
    // ECSを正規データとしてThermalWorldの非所有Registryを再構築します。
    static void SynchronizeWorld(Scene& scene);

    // Rigid Bodyの解決済みContact ManifoldをThermalContactへ追加変換します。
    static void AppendRigidBodyContacts(
        Scene& scene,
        const std::vector<ContactManifold>& manifolds);
};

} // namespace ph
} // namespace Raven
