#pragma once

#include <vector>

namespace Raven
{
class Scene;

namespace ph
{
struct ContactManifold;

class ThermalSystem
{
public:
    static void SynchronizeWorld(Scene& scene);
    static void AppendRigidBodyContacts(
        Scene& scene,
        const std::vector<ContactManifold>& manifolds);
};

} // namespace ph
} // namespace Raven
