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
    // 線形運動量 -1 が保存されることを確認します。Sphere中心を通るため回転項は0です。
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

    // Box上面の中心からx方向へずれた位置へParticleを落とし、回転有効質量がImpulse分母へ
    // 入ることを確認します。m_p=m_b=1, r=(0.5,1,0), n=(0,1,0), Izz^-1=1.5 なので
    // K = 1 + 1 + 0.375 = 2.375、J = 1 / 2.375 です。
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
        collider.Restitution = 0.0f;
        bodyEntity.AddComponent<ColliderComponent>(collider);

        std::vector<FluidParticle> particles(1u);
        particles[0].Position = { 0.5f, 1.05f, 0.0f };
        particles[0].Velocity = { 0.0f, -1.0f, 0.0f };
        particles[0].Mass = 1.0f;

        FluidRigidBodyCouplingSettings settings{};
        settings.ParticleRadius = 0.1f;
        settings.Restitution = 0.0f;
        FluidRigidBodyCoupling coupling(settings);
        coupling.ResolveScene(scene, scene.GetPhysicsWorld(), particles);

        const RigidBodyComponent& resolvedBody =
            bodyEntity.GetComponent<RigidBodyComponent>();
        const float expectedImpulse = 1.0f / 2.375f;
        assert(std::abs(particles[0].Velocity.y - (-1.0f + expectedImpulse)) <= 1.0e-5f);
        assert(std::abs(resolvedBody.LinearVelocity.y + expectedImpulse) <= 1.0e-5f);
        assert(std::abs(resolvedBody.AngularVelocity.z + expectedImpulse * 0.75f) <= 1.0e-5f);

        // Internal impulseなのでFluid + RigidBody全体の線形運動量は保存されます。
        const float momentum = particles[0].Mass * particles[0].Velocity.y
            + resolvedBody.Mass * resolvedBody.LinearVelocity.y;
        assert(std::abs(momentum + 1.0f) <= 1.0e-5f);
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
