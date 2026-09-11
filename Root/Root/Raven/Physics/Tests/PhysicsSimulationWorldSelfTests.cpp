#include "Raven/Physics/PhysicsSimulationWorld.h"

#include <cassert>
#include <cstdint>

#include "Raven/Physics/SoftBody/SoftBodySolver.h"

namespace Raven::ph::tests
{
namespace
{
class TestSoftBodySimulationParticipant final : public SoftBodySimulationParticipant
{
public:
    void SimulateSoftBody(float fixedDeltaTime) override
    {
        ++StepCount;
        LastFixedDeltaTime = fixedDeltaTime;
    }

    void SynchronizeSoftBodyOutput() override
    {
        ++SynchronizationCount;
    }

    uint32_t StepCount = 0u;
    uint32_t SynchronizationCount = 0u;
    float LastFixedDeltaTime = 0.0f;
};
}

// PhysicsSimulationWorldがRigid Body / Soft Body Domainの入口を単一所有し、
// SoftBodyWorldがSolver参照とFixed Step Participantを重複なく非所有管理できることを確認します。
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
    assert(softBodyWorld.GetRegisteredSimulationParticipantCount() == 0u);

    SoftBodySolver firstSolver;
    SoftBodySolver secondSolver;

    assert(softBodyWorld.RegisterSolver(firstSolver) == true);
    assert(softBodyWorld.RegisterSolver(firstSolver) == false);
    assert(softBodyWorld.RegisterSolver(secondSolver) == true);
    assert(softBodyWorld.GetRegisteredSolverCount() == 2u);
    assert(softBodyWorld.ContainsSolver(firstSolver) == true);
    assert(softBodyWorld.ContainsSolver(secondSolver) == true);

    TestSoftBodySimulationParticipant firstParticipant;
    TestSoftBodySimulationParticipant secondParticipant;

    assert(softBodyWorld.RegisterSimulationParticipant(firstParticipant) == true);
    assert(softBodyWorld.RegisterSimulationParticipant(firstParticipant) == false);
    assert(softBodyWorld.RegisterSimulationParticipant(secondParticipant) == true);
    assert(softBodyWorld.GetRegisteredSimulationParticipantCount() == 2u);

    constexpr float fixedDeltaTime = 1.0f / 60.0f;
    softBodyWorld.Step(fixedDeltaTime);

    // 互換Stepは従来どおりSimulation -> Output同期を1回ずつ実行します。
    assert(firstParticipant.StepCount == 1u);
    assert(secondParticipant.StepCount == 1u);
    assert(firstParticipant.SynchronizationCount == 1u);
    assert(secondParticipant.SynchronizationCount == 1u);
    assert(firstParticipant.LastFixedDeltaTime == fixedDeltaTime);
    assert(secondParticipant.LastFixedDeltaTime == fixedDeltaTime);

    // catch-upを模した2回のSimulationでは、途中StateをOutputへ同期しません。
    softBodyWorld.StepSimulation(fixedDeltaTime);
    softBodyWorld.StepSimulation(fixedDeltaTime);
    assert(firstParticipant.StepCount == 3u);
    assert(secondParticipant.StepCount == 3u);
    assert(firstParticipant.SynchronizationCount == 1u);
    assert(secondParticipant.SynchronizationCount == 1u);

    // Application frameの最終Fixed Step後に1回だけ最新Stateを同期します。
    softBodyWorld.SynchronizeOutputs();
    assert(firstParticipant.SynchronizationCount == 2u);
    assert(secondParticipant.SynchronizationCount == 2u);

    assert(softBodyWorld.UnregisterSimulationParticipant(firstParticipant) == true);
    assert(softBodyWorld.UnregisterSimulationParticipant(firstParticipant) == false);
    assert(softBodyWorld.ContainsSimulationParticipant(firstParticipant) == false);

    // Unregister後は残ったParticipantだけがSimulation/同期されます。
    softBodyWorld.Step(fixedDeltaTime);
    assert(firstParticipant.StepCount == 3u);
    assert(firstParticipant.SynchronizationCount == 2u);
    assert(secondParticipant.StepCount == 4u);
    assert(secondParticipant.SynchronizationCount == 3u);

    assert(softBodyWorld.UnregisterSolver(firstSolver) == true);
    assert(softBodyWorld.UnregisterSolver(firstSolver) == false);
    assert(softBodyWorld.ContainsSolver(firstSolver) == false);

    softBodyWorld.Clear();
    assert(softBodyWorld.GetRegisteredSolverCount() == 0u);
    assert(softBodyWorld.GetRegisteredSimulationParticipantCount() == 0u);
}

}
