#include "Raven/Physics/Electromagnetism/ElectromagneticSystem.h"

#include <cstddef>
#include <vector>

#include "Raven/Physics/Electromagnetism/ElectricCharge.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
struct ChargedRigidBodyReference
{
    math::Vec3 Position{};
    RigidBodyComponent* RigidBody = nullptr;
    double ChargeCoulombs = 0.0;
};

bool CanReceiveForce(const RigidBodyComponent* rigidBody)
{
    return rigidBody != nullptr
        && rigidBody->Type == BodyType::Dynamic
        && rigidBody->InverseMass > 0.0f;
}

void WakeRigidBody(RigidBodyComponent& rigidBody)
{
    rigidBody.IsSleeping = false;
    rigidBody.SleepTimer = 0.0f;
}
}

void ElectromagneticSystem::ApplyCoulombForces(Scene& scene) const
{
    std::vector<ChargedRigidBodyReference> chargedBodies;

    for (auto [entity, transform, rigidBody, collider, electricCharge]
        : scene.View<TransformComponent, RigidBodyComponent, ColliderComponent, ElectricChargeComponent>())
    {
        static_cast<void>(entity);
        static_cast<void>(collider);

        if (electricCharge.IsEnabled == false || electricCharge.ChargeCoulombs == 0.0)
        {
            continue;
        }

        ChargedRigidBodyReference reference{};
        reference.Position = transform.Position;
        reference.RigidBody = &rigidBody;
        reference.ChargeCoulombs = electricCharge.ChargeCoulombs;
        chargedBodies.push_back(reference);
    }

    // 各ペアを1度だけ評価し、作用反作用を同じ計算結果から同時に加えます。
    // 個別にA->B / B->Aを再計算しないことで、丸め誤差による運動量の非対称を避けます。
    for (std::size_t i = 0; i < chargedBodies.size(); ++i)
    {
        for (std::size_t j = i + 1; j < chargedBodies.size(); ++j)
        {
            ChargedRigidBodyReference& a = chargedBodies[i];
            ChargedRigidBodyReference& b = chargedBodies[j];

            const math::Vec3 forceOnB = ComputeCoulombForce(
                a.Position,
                a.ChargeCoulombs,
                b.Position,
                b.ChargeCoulombs,
                m_CoulombForceSettings);

            if (CanReceiveForce(a.RigidBody))
            {
                a.RigidBody->Force -= forceOnB;
                if (forceOnB.LengthSq() > 0.0f)
                {
                    WakeRigidBody(*a.RigidBody);
                }
            }

            if (CanReceiveForce(b.RigidBody))
            {
                b.RigidBody->Force += forceOnB;
                if (forceOnB.LengthSq() > 0.0f)
                {
                    WakeRigidBody(*b.RigidBody);
                }
            }
        }
    }
}

} // namespace Raven::ph
