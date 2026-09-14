#include "Raven/Physics/Tests/FluidCouplingMeasurementSelfTests.h"

#include <cassert>
#include <cmath>
#include <vector>

#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph::tests
{
namespace
{
class TestFluidCouplingMeasurementParticipant final : public FluidSimulationParticipant
{
public:
    explicit TestFluidCouplingMeasurementParticipant(std::vector<FluidParticle>& particles)
        : m_Particles(particles)
    {
        m_CouplingBinding.Particles = &m_Particles;
        m_CouplingBinding.StaticColliderCouplingEnabled = false;
        m_CouplingBinding.RigidBodyCouplingEnabled = true;
        m_CouplingBinding.RigidBodySettings.ParticleRadius = 0.1f;
        m_CouplingBinding.RigidBodySettings.Restitution = 0.0f;
        m_CouplingBinding.RigidBodySettings.DragCoefficient = 0.0f;
        m_CouplingBinding.RigidBodySettings.PressureReactionCoefficient = 0.0f;
        m_CouplingBinding.RigidBodySettings.BuoyancyCoefficient = 0.0f;
    }

    void SimulateFluid(float fixedDeltaTime) override
    {
        static_cast<void>(fixedDeltaTime);

        if (m_Particles.empty() == true)
        {
            return;
        }

        // 毎Fixed Stepで同じ接触条件を再構築し、render frameではなくWorld Step回数そのものが
        // Measurementへ反映されることを検証できるようにします。
        m_Particles[0].Position = { 0.0f, 0.5f, 0.0f };
        m_Particles[0].Velocity = { 0.0f, -1.0f, 0.0f };
        m_Particles[0].Mass = 1.0f;
        m_Particles[0].Pressure = 0.0f;
    }

    FluidCouplingBinding* GetFluidCouplingBinding() override
    {
        return &m_CouplingBinding;
    }

    void SetRigidBodyCouplingEnabled(bool enabled)
    {
        m_CouplingBinding.RigidBodyCouplingEnabled = enabled;
    }

private:
    std::vector<FluidParticle>& m_Particles;
    FluidCouplingBinding m_CouplingBinding{};
};

bool IsNearlyEqual(float left, float right)
{
    return std::abs(left - right) <= 1.0e-5f;
}
}

void RunFluidCouplingMeasurementSelfTests()
{
    Scene scene;
    PhysicsSimulationWorld& simulationWorld = scene.GetPhysicsSimulationWorld();
    FluidWorld& fluidWorld = simulationWorld.GetFluidWorld();
    PhysicsWorld& rigidBodyWorld = simulationWorld.GetRigidBodyWorld();

    Entity bodyEntity = scene.CreateEntity("Fluid Coupling Measurement Sphere");
    RigidBodyComponent rigidBody{};
    rigidBody.SetBodyType(BodyType::Dynamic);
    rigidBody.SetMass(2.0f);
    rigidBody.UseGravity = false;
    rigidBody.AllowSleep = false;
    bodyEntity.AddComponent<RigidBodyComponent>(rigidBody);

    ColliderComponent collider{};
    collider.Type = ColliderType::Sphere;
    collider.Radius = 0.5f;
    collider.Restitution = 0.0f;
    bodyEntity.AddComponent<ColliderComponent>(collider);

    std::vector<FluidParticle> particles(1u);
    TestFluidCouplingMeasurementParticipant participant(particles);
    assert(fluidWorld.RegisterSimulationParticipant(participant) == true);

    constexpr float fixedDeltaTime = 0.1f;
    fluidWorld.ResetCouplingMeasurement();
    fluidWorld.SetCouplingMeasurementEnabled(true);

    fluidWorld.StepSimulation(scene, rigidBodyWorld, fixedDeltaTime);
    fluidWorld.StepSimulation(scene, rigidBodyWorld, fixedDeltaTime);

    const FluidCouplingMeasurement& measurement = fluidWorld.GetCouplingMeasurement();

    // Application frameから独立してFluidWorld::StepSimulation()呼び出しごとに1 sampleを記録します。
    // catch-upで同じframe中に2回実行された場合も、FixedStepCountと固定時間が2 Step分進みます。
    assert(measurement.FixedStepCount == 2u);
    assert(IsNearlyEqual(measurement.ElapsedFixedTime, fixedDeltaTime * 2.0f));
    assert(measurement.ResolvedContactCount == 2u);
    assert(measurement.TotalNormalImpulse > 0.0f);

    fluidWorld.SetCouplingMeasurementEnabled(false);
    fluidWorld.StepSimulation(scene, rigidBodyWorld, fixedDeltaTime);
    assert(fluidWorld.GetCouplingMeasurement().FixedStepCount == 2u);
    assert(IsNearlyEqual(
        fluidWorld.GetCouplingMeasurement().ElapsedFixedTime,
        fixedDeltaTime * 2.0f));

    // ResetはPause状態を維持して累積値だけを消します。
    fluidWorld.ResetCouplingMeasurement();
    assert(fluidWorld.IsCouplingMeasurementEnabled() == false);
    assert(fluidWorld.GetCouplingMeasurement().FixedStepCount == 0u);
    assert(IsNearlyEqual(fluidWorld.GetCouplingMeasurement().ElapsedFixedTime, 0.0f));

    // Coupling OffでもMeasurement自体はFixed Step境界で進みます。
    // これによりPreset間で同じ固定時間幅を基準に比較できます。
    participant.SetRigidBodyCouplingEnabled(false);
    fluidWorld.SetCouplingMeasurementEnabled(true);
    fluidWorld.StepSimulation(scene, rigidBodyWorld, fixedDeltaTime);

    const FluidCouplingMeasurement& disabledCouplingMeasurement = fluidWorld.GetCouplingMeasurement();
    assert(disabledCouplingMeasurement.FixedStepCount == 1u);
    assert(IsNearlyEqual(disabledCouplingMeasurement.ElapsedFixedTime, fixedDeltaTime));
    assert(disabledCouplingMeasurement.ResolvedContactCount == 0u);
    assert(IsNearlyEqual(disabledCouplingMeasurement.TotalNormalImpulse, 0.0f));

    assert(fluidWorld.UnregisterSimulationParticipant(participant) == true);
}

} // namespace Raven::ph::tests
