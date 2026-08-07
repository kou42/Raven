#pragma once

#include "Raven/Physics/Contact.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{

bool GenerateSphereSphereManifold(
    Entity sphereEntityA,
    const TransformComponent& sphereTransformA,
    const ColliderComponent& sphereColliderA,
    Entity sphereEntityB,
    const TransformComponent& sphereTransformB,
    const ColliderComponent& sphereColliderB,
    ContactManifold& outManifold);

bool GenerateSpherePlaneManifold(
    Entity sphereEntity,
    const TransformComponent& sphereTransform,
    const ColliderComponent& sphereCollider,
    Entity planeEntity,
    const TransformComponent& planeTransform,
    const ColliderComponent& planeCollider,
    ContactManifold& outManifold);

// ============================================================================
// GenerateSphereBoxManifold
// ============================================================================
// 現在のBox Colliderは回転しないWorld AABBとして扱うため、Sphere中心からBoxへの
// 最近接点を求めることで1点Manifoldを生成できます。
//
// 引数順序は必ず A=Sphere / B=Box です。
// ContactManifold::Normalもこの規約に従い「SphereからBoxへ向く法線」を返します。
//
// Sphere中心がBox内部にある場合、通常のclosest-pointでは差ベクトルが0になります。
// そのため最も近いBox面を選択し、SolverがSphereをその面の外へ押し出す向きになる
// ように法線と貫通量を明示的に構築します。
bool GenerateSphereBoxManifold(
    Entity sphereEntity,
    const TransformComponent& sphereTransform,
    const ColliderComponent& sphereCollider,
    Entity boxEntity,
    const TransformComponent& boxTransform,
    const ColliderComponent& boxCollider,
    ContactManifold& outManifold);

} // namespace Raven::ph
