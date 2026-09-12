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
    // 1次元の完全非弾性接触です。
    // Particle mass=1, Body mass=2, 初期速度(-1, 0)なので、接触後は双方-1/3へ揃い、
    // 線形運動量 -1 が保存されることを確認します。
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
        collider.Restitution = 0.0f;
        bodyEntity.AddComponent<ColliderComponent>(collider);

        std::vector<FluidParticle> particles(1u);
        particles[0].Position = { 0.4f, 0.0f, 0.0f };
        particles[0].Velocity = { -1.0f, 0.0f, 0.0f };
        particles[0].Mass = 1.0f;

        FluidRigidBodyCouplingSettings settings{};
        settings.ParticleRadius = 0.1f;
        settings.Restitution = 0.0f;
        FluidRigidBodyCoupling coupling(settings);
        PhysicsWorld& physicsWorld = scene.GetPhysicsWorld();

        coupling.ResolveScene(scene, physicsWorld, particles);

        const RigidBodyComponent& resolvedBody =
            bodyEntity.GetComponent<RigidBodyComponent>();
        const float expectedVelocity = -1.0f / 3.0f;
        assert(std::abs(particles[0].Velocity.x - expectedVelocity) <= 1.0e-5f);
        assert(std::abs(resolvedBody.LinearVelocity.x - expectedVelocity) <= 1.0e-5f);

        const float momentum = particles[0].Mass * particles[0].Velocity.x
            + resolvedBody.Mass * resolvedBody.LinearVelocity.x;
        assert(std::abs(momentum + 1.0f) <= 1.0e-5f);
        assert(coupling.GetLastStatistics().AppliedImpulseCount == 1u);
    }

    // TriggerはFluidとの物理反応を発生させません。
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
