#pragma once

#include "Raven/Physics/Contact.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{

// Sphere-Sphere:
// 中心距離と半径和から接触判定し、1接触点マニホールドを生成します。
bool GenerateSphereSphereManifold(
    Entity sphereEntityA,
    const TransformComponent& sphereTransformA,
    const ColliderComponent& sphereColliderA,
    Entity sphereEntityB,
    const TransformComponent& sphereTransformB,
    const ColliderComponent& sphereColliderB,
    ContactManifold& outManifold);

// Sphere-Plane:
// 平面への符号付き距離から貫通量を求め、法線方向をA->B規約へ合わせます。
bool GenerateSpherePlaneManifold(
    Entity sphereEntity,
    const TransformComponent& sphereTransform,
    const ColliderComponent& sphereCollider,
    Entity planeEntity,
    const TransformComponent& planeTransform,
    const ColliderComponent& planeCollider,
    ContactManifold& outManifold);

// Box-Plane:
// OBBの8頂点を平面へ投影し、平面を越えた頂点を最大4点のContact Manifoldへまとめます。
// Boxが回転していてもComputeBoxOBB()のワールド頂点を使うため、面・辺・頂点接触を
// 同じ処理で扱えます。法線は既存Manifold規約に合わせて A(Box) -> B(Plane) 方向です。
bool GenerateBoxPlaneManifold(
    Entity boxEntity,
    const TransformComponent& boxTransform,
    const ColliderComponent& boxCollider,
    Entity planeEntity,
    const TransformComponent& planeTransform,
    const ColliderComponent& planeCollider,
    ContactManifold& outManifold);

// Sphere-Box:
// OBBローカルで最近接点を求め、外部/内部の両ケースで法線と貫通量を計算します。
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
