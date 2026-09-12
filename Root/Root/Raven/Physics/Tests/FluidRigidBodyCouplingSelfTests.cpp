#include <cassert>
#include <cmath>
#include <vector>

#include "Raven/Physics/Coupling/FluidRigidBodyCoupling.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph::tests
{

void RunFluidRigidBodyCouplingSelfTests()
{
    {
        Scene scene{};
        Entity bodyEntity = scene.CreateEntity("Fluid Coupling Dynamic Sphere");
        TransformComponent& transform = bodyEntity.GetComponent<TransformComponent>();
        transform.Position = math::Vec3{};
        RigidBodyComponent rigidBody{};
        rigidBody.SetBodyType(BodyType::Dynamic);
        rigidBody.SetMass(2.0f);
        rigidBody.UseGravity = false;
        bodyEntity.AddComponent<RigidBodyComponent>(rigidBody);
        ColliderComponent collider{};
        collider.Type = ColliderType::Sphere;
        collider.Radius = 0.5f;
        bodyEntity.AddComponent<ColliderComponent>(collider);
        std::vector<FluidParticle> particles(1u);
        particles[0].Position = { 0.4f, 0.0f, 0.0f };
        particles[0].Velocity = { -1.0f, 0.0f, 0.0f };
        particles[0].Mass = 1.0f;
        FluidRigidBodyCouplingSettings settings{};
        settings.ParticleRadius = 0.1f;
        FluidRigidBodyCoupling coupling(settings);
        coupling.ResolveScene(scene, scene.GetPhysicsWorld(), particles);
        const RigidBodyComponent& resolvedBody = bodyEntity.GetComponent<RigidBodyComponent>();
        const float expectedVelocity = -1.0f / 3.0f;
        assert(std::abs(particles[0].Velocity.x - expectedVelocity) <= 1.0e-5f);
        assert(std::abs(resolvedBody.LinearVelocity.x - expectedVelocity) <= 1.0e-5f);
        const float momentum = particles[0].Mass * particles[0].Velocity.x + resolvedBody.Mass * resolvedBody.LinearVelocity.x;
        assert(std::abs(momentum + 1.0f) <= 1.0e-5f);
    }

    {
        Scene scene{};
        Entity bodyEntity = scene.CreateEntity("Fluid Coupling Dynamic Box");
        TransformComponent& transform = bodyEntity.GetComponent<TransformComponent>();
        transform.Position = math::Vec3{};
        RigidBodyComponent rigidBody{};
        rigidBody.SetBodyType(BodyType::Dynamic);
        rigidBody.SetMass(1.0f);
        rigidBody.UseGravity = false;
        bodyEntity.AddComponent<RigidBodyComponent>(rigidBody);
        ColliderComponent collider{};
        collider.Type = ColliderType::Box;
        collider.HalfExtents = { 1.0f, 1.0f, 1.0f };
        bodyEntity.AddComponent<ColliderComponent>(collider);
        std::vector<FluidParticle> particles(1u);
        particles[0].Position = { 0.5f, 1.05f, 0.0f };
        particles[0].Velocity = { 0.0f, -1.0f, 0.0f };
        particles[0].Mass = 1.0f;
        FluidRigidBodyCouplingSettings settings{};
        settings.ParticleRadius = 0.1f;
        FluidRigidBodyCoupling coupling(settings);
        coupling.ResolveScene(scene, scene.GetPhysicsWorld(), particles);
        const RigidBodyComponent& resolvedBody = bodyEntity.GetComponent<RigidBodyComponent>();
        const float expectedImpulse = 1.0f / 2.375f;
        assert(std::abs(particles[0].Velocity.y - (-1.0f + expectedImpulse)) <= 1.0e-5f);
        assert(std::abs(resolvedBody.LinearVelocity.y + expectedImpulse) <= 1.0e-5f);
        assert(std::abs(resolvedBody.AngularVelocity.z + expectedImpulse * 0.75f) <= 1.0e-5f);
    }

    // Sphere上面へわずかに貫通したParticleを接線方向へ滑らせ、Dragを確認します。
    // DragはInternal Impulseなのでx方向線形運動量を保存しつつ、接触点のr x JでBodyを回転させます。
    {
        Scene scene{};
        Entity bodyEntity = scene.CreateEntity("Fluid Coupling Drag Sphere");
        RigidBodyComponent rigidBody{};
        rigidBody.SetBodyType(BodyType::Dynamic);
        rigidBody.SetMass(1.0f);
        rigidBody.UseGravity = false;
        bodyEntity.AddComponent<RigidBodyComponent>(rigidBody);
        ColliderComponent collider{};
        collider.Type = ColliderType::Sphere;
        collider.Radius = 0.5f;
        bodyEntity.AddComponent<ColliderComponent>(collider);

        std::vector<FluidParticle> particles(1u);
        particles[0].Position = { 0.0f, 0.55f, 0.0f };
        particles[0].Velocity = { 1.0f, 0.0f, 0.0f };
        particles[0].Mass = 1.0f;

        FluidRigidBodyCouplingSettings settings{};
        settings.ParticleRadius = 0.1f;
        settings.DragCoefficient = 1.0f;
        FluidRigidBodyCoupling coupling(settings);
        coupling.ResolveScene(scene, scene.GetPhysicsWorld(), particles);

        const RigidBodyComponent& resolvedBody = bodyEntity.GetComponent<RigidBodyComponent>();
        assert(particles[0].Velocity.x < 1.0f);
        assert(resolvedBody.LinearVelocity.x > 0.0f);
        assert(std::abs(resolvedBody.AngularVelocity.z) > 0.0f);
        const float momentum = particles[0].Velocity.x + resolvedBody.LinearVelocity.x;
        assert(std::abs(momentum - 1.0f) <= 1.0e-5f);
        assert(coupling.GetLastStatistics().AppliedDragImpulseCount == 1u);
        assert(coupling.GetLastStatistics().TotalDragImpulse > 0.0f);
    }

    // 正圧pをParticleの投影面積pi*r^2へ作用させ、J=p*A*dtの反作用が
    // ParticleとRigidBodyへ等量反対向きに入ることを確認します。
    {
        Scene scene{};
        Entity bodyEntity = scene.CreateEntity("Fluid Coupling Pressure Sphere");
        RigidBodyComponent rigidBody{};
        rigidBody.SetBodyType(BodyType::Dynamic);
        rigidBody.SetMass(1.0f);
        rigidBody.UseGravity = false;
        bodyEntity.AddComponent<RigidBodyComponent>(rigidBody);

        ColliderComponent collider{};
        collider.Type = ColliderType::Sphere;
        collider.Radius = 0.5f;
        bodyEntity.AddComponent<ColliderComponent>(collider);

        std::vector<FluidParticle> particles(1u);
        particles[0].Position = { 0.0f, 0.55f, 0.0f };
        particles[0].Velocity = {};
        particles[0].Mass = 1.0f;
        particles[0].Pressure = 10.0f;

        FluidRigidBodyCouplingSettings settings{};
        settings.ParticleRadius = 0.1f;
        settings.PressureReactionCoefficient = 1.0f;
        FluidRigidBodyCoupling coupling(settings);
        constexpr float deltaTime = 0.1f;
        coupling.ResolveScene(scene, scene.GetPhysicsWorld(), particles, deltaTime);

        const RigidBodyComponent& resolvedBody = bodyEntity.GetComponent<RigidBodyComponent>();
        constexpr float pi = 3.14159265358979323846f;
        const float expectedImpulse = 10.0f * pi * 0.1f * 0.1f * deltaTime;
        assert(std::abs(particles[0].Velocity.y - expectedImpulse) <= 1.0e-5f);
        assert(std::abs(resolvedBody.LinearVelocity.y + expectedImpulse) <= 1.0e-5f);
        assert(std::abs(resolvedBody.AngularVelocity.Length()) <= 1.0e-5f);

        const float momentum = particles[0].Velocity.y + resolvedBody.LinearVelocity.y;
        assert(std::abs(momentum) <= 1.0e-5f);
        assert(coupling.GetLastStatistics().AppliedPressureImpulseCount == 1u);
        assert(std::abs(coupling.GetLastStatistics().TotalPressureImpulse - expectedImpulse) <= 1.0e-5f);
    }

    {
        Scene scene{};
        Entity bodyEntity = scene.CreateEntity("Fluid Coupling Trigger");
        RigidBodyComponent rigidBody{};
        rigidBody.SetBodyType(BodyType::Dynamic);
        rigidBody.SetMass(1.0f);
        bodyEntity.AddComponent<RigidBodyComponent>(rigidBody);
        ColliderComponent collider{};
        collider.Type = ColliderType::Sphere;
        collider.Radius = 1.0f;
        collider.IsTrigger = true;
        bodyEntity.AddComponent<ColliderComponent>(collider);
        std::vector<FluidParticle> particles(1u);
        particles[0].Position = { 0.5f, 0.0f, 0.0f };
        particles[0].Velocity = { -1.0f, 0.0f, 0.0f };
        FluidRigidBodyCoupling coupling{};
        coupling.ResolveScene(scene, scene.GetPhysicsWorld(), particles);
        assert(std::abs(particles[0].Velocity.x + 1.0f) <= 1.0e-6f);
        assert(coupling.GetLastStatistics().ResolvedContactCount == 0u);
    }
}

} // namespace Raven::ph::tests
