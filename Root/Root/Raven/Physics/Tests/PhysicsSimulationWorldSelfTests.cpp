#include "Raven/Physics/PhysicsSimulationWorld.h"

#include <cassert>

#include "Raven/Physics/SoftBody/SoftBodySolver.h"

namespace Raven::ph::tests
{

// PhysicsSimulationWorldがRigid Body / Soft Body Domainの入口を単一所有し、
// SoftBodyWorld Registryが非所有参照を重複なく管理できることを確認します。
void RunPhysicsSimulationWorldSelfTests()
{
    PhysicsSimulationWorld simulationWorld;

    PhysicsWorld& rigidBodyWorld = simulationWorld.GetRigidBodyWorld();
    const PhysicsSimulationWorld& constSimulationWorld = simulationWorld;
    const PhysicsWorld& constRigidBodyWorld = constSimulationWorld.GetRigidBodyWorld();

    assert(&rigidBodyWorld == &constRigidBodyWorld);

    SoftBodyWorld& softBodyWorld = simulationWorld.GetSoftBodyWorld();
    const SoftBodyWorld& constSoftBodyWorld = constSimulationWorld.GetSoftBodyWorld();

    assert(&softBodyWorld == &constSoftBodyWorld);
    assert(softBodyWorld.GetRegisteredSolverCount() == 0u);

    SoftBodySolver firstSolver;
    SoftBodySolver secondSolver;

    assert(softBodyWorld.RegisterSolver(firstSolver) == true);
    assert(softBodyWorld.RegisterSolver(firstSolver) == false);
    assert(softBodyWorld.RegisterSolver(secondSolver) == true);
    assert(softBodyWorld.GetRegisteredSolverCount() == 2u);
    assert(softBodyWorld.ContainsSolver(firstSolver) == true);
    assert(softBodyWorld.ContainsSolver(secondSolver) == true);

    assert(softBodyWorld.UnregisterSolver(firstSolver) == true);
    assert(softBodyWorld.UnregisterSolver(firstSolver) == false);
    assert(softBodyWorld.ContainsSolver(firstSolver) == false);
    assert(softBodyWorld.GetRegisteredSolverCount() == 1u);

    softBodyWorld.Clear();
    assert(softBodyWorld.GetRegisteredSolverCount() == 0u);
}

}
