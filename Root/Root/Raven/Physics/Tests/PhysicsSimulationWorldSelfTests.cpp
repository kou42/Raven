#include "Raven/Physics/PhysicsSimulationWorld.h"

#include <cassert>

namespace Raven::ph::tests
{

// Phase 0の責務は上位Worldが既存Rigid Body Worldを所有し、
// const / non-constの両方から同一インスタンスへ到達できることです。
// Physics挙動そのものは既存PhysicsWorldのSelf Testへ委ねます。
void RunPhysicsSimulationWorldSelfTests()
{
    PhysicsSimulationWorld simulationWorld;

    PhysicsWorld& rigidBodyWorld = simulationWorld.GetRigidBodyWorld();
    const PhysicsSimulationWorld& constSimulationWorld = simulationWorld;
    const PhysicsWorld& constRigidBodyWorld = constSimulationWorld.GetRigidBodyWorld();

    assert(&rigidBodyWorld == &constRigidBodyWorld);
}

}
