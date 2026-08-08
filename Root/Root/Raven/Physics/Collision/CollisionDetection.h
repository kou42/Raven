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

bool GenerateSphereBoxManifold(
    Entity sphereEntity,
    const TransformComponent& sphereTransform,
    const ColliderComponent& sphereCollider,
    Entity boxEntity,
    const TransformComponent& boxTransform,
    const ColliderComponent& boxCollider,
    ContactManifold& outManifold);

// ============================================================================
// GenerateBoxBoxManifold
// ============================================================================
// Box ColliderはTransform::Rotationを反映したOBBとして判定します。
// Narrow PhaseではOBB-OBBの15軸SATを使い、最小貫通軸のFeature種別に応じて
// Face-FaceはReference/Incident Face clipping、Edge-Edgeは辺同士の最近接点から
// Contact Manifoldを生成します。
//
// API自体はAABB時代から維持しているため、PhysicsWorld側のdispatchは変更不要です。
bool GenerateBoxBoxManifold(
    Entity boxEntityA,
    const TransformComponent& boxTransformA,
    const ColliderComponent& boxColliderA,
    Entity boxEntityB,
    const TransformComponent& boxTransformB,
    const ColliderComponent& boxColliderB,
    ContactManifold& outManifold);

} // namespace Raven::ph
