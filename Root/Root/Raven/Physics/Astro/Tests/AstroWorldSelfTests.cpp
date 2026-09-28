#include "Raven/Physics/Astro/Tests/AstroWorldSelfTests.h"

#include <cassert>
#include <cmath>
#include <vector>

#include "Raven/Physics/Astro/AstroWorld.h"
#include "Raven/Physics/Astro/CelestialBody.h"
#include "Raven/Physics/Astro/Gravity/DirectGravitySolver.h"
#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph::tests
{
namespace
{
bool NearlyEqual(float left, float right, float epsilon = 1.0e-5f)
{
    return std::abs(left - right) <= epsilon;
}

Entity CreateCelestialBody(
    Scene& scene,
    const char* name,
    const math::Vec3& position,
    float mass,
    BodyType bodyType = BodyType::Dynamic)
{
    Entity entity = scene.CreateEntity(name);
    entity.GetComponent<TransformComponent>().Position = position;

    RigidBodyComponent rigidBody{};
    rigidBody.SetMass(mass);
    rigidBody.SetBodyType(bodyType);
    rigidBody.UseGravity = false;
    rigidBody.AllowSleep = false;
    rigidBody.LinearDamping = 0.0f;
    rigidBody.AngularDamping = 0.0f;
    entity.AddComponent<RigidBodyComponent>(rigidBody);
    entity.AddComponent<CelestialBodyComponent>(CelestialBodyComponent{});
    return entity;
}
}

void RunAstroWorldSelfTests()
{
    DirectGravitySolver solver;
    GravitySolverSettings settings{};
    settings.GravitationalConstant = 1.0;
    settings.MinimumDistance = 0.01;

    std::vector<AstroBodyState> bodies(2u);
    bodies[0].Position = { 0.0f, 0.0f, 0.0f };
    bodies[0].Mass = 2.0;
    bodies[1].Position = { 2.0f, 0.0f, 0.0f };
    bodies[1].Mass = 3.0;

    std::vector<math::Vec3> forces;
    solver.ComputeForces(bodies, settings, forces);

    // G=1, m1=2, m2=3, r=2 なので |F|=1.5。両Bodyへ同じ大きさを逆向きに加えます。
    assert(forces.size() == 2u);
    assert(NearlyEqual(forces[0].x, 1.5f));
    assert(NearlyEqual(forces[1].x, -1.5f));
    assert(NearlyEqual(forces[0].x + forces[1].x, 0.0f));

    bodies[1].Position = { 4.0f, 0.0f, 0.0f };
    solver.ComputeForces(bodies, settings, forces);
    assert(NearlyEqual(forces[0].x, 0.375f));

    bodies[1].Mass = 6.0;
    solver.ComputeForces(bodies, settings, forces);
    assert(NearlyEqual(forces[0].x, 0.75f));

    bodies[1].Position = bodies[0].Position;
    solver.ComputeForces(bodies, settings, forces);
    assert(std::isfinite(forces[0].x));
    assert(std::isfinite(forces[1].x));
    assert(NearlyEqual(forces[0].LengthSq(), 0.0f));

    // Generate/Receiveを分離したStatic sourceでは、source自身を動かさずprobeだけへ引力を加えられます。
    bodies[0].Position = { 0.0f, 0.0f, 0.0f };
    bodies[0].Mass = 10.0;
    bodies[0].GenerateGravity = true;
    bodies[0].ReceiveGravity = false;
    bodies[1].Position = { 2.0f, 0.0f, 0.0f };
    bodies[1].Mass = 1.0;
    bodies[1].GenerateGravity = false;
    bodies[1].ReceiveGravity = true;
    solver.ComputeForces(bodies, settings, forces);
    assert(NearlyEqual(forces[0].LengthSq(), 0.0f));
    assert(NearlyEqual(forces[1].x, -2.5f));

    // PhysicsSimulationWorldではAstro重力をElectromagnetismと同様にRigid積分前のForceへ蓄積します。
    Scene scene;
    PhysicsSimulationWorld& simulationWorld = scene.GetPhysicsSimulationWorld();
    GravitySolverSettings worldSettings{};
    worldSettings.GravitationalConstant = 1.0;
    worldSettings.MinimumDistance = 0.01;
    simulationWorld.GetAstroWorld().SetGravitySolverSettings(worldSettings);

    Entity a = CreateCelestialBody(scene, "Astro A", { 0.0f, 0.0f, 0.0f }, 2.0f);
    Entity b = CreateCelestialBody(scene, "Astro B", { 2.0f, 0.0f, 0.0f }, 3.0f);

    constexpr float fixedDeltaTime = 0.1f;
    simulationWorld.StepSimulation(scene, fixedDeltaTime);

    const RigidBodyComponent& rigidBodyA = a.GetComponent<RigidBodyComponent>();
    const RigidBodyComponent& rigidBodyB = b.GetComponent<RigidBodyComponent>();
    assert(NearlyEqual(rigidBodyA.LinearVelocity.x, 0.075f));
    assert(NearlyEqual(rigidBodyB.LinearVelocity.x, -0.05f));
    assert(rigidBodyA.Force.LengthSq() <= 1.0e-12f);
    assert(rigidBodyB.Force.LengthSq() <= 1.0e-12f);
}

} // namespace Raven::ph::tests
