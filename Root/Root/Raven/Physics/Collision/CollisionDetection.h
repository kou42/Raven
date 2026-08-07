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
// 現在のRaven Box Colliderは回転しないAABBです。
// したがってOBB用15軸SATではなく、world X/Y/Zの3軸で分離を確認し、
// 最小貫通軸をContact Normalとして採用します。
//
// 接触面がface-faceの場合は、2つのBoxの重なり矩形のcornerを最大4点登録します。
// これによりContactManifoldの複数点構造を実際のSolverへ流せる最初の形状になります。
// 将来Box Rotationを有効化した時、このAPIを維持したまま内部を15軸OBB SAT +
// clippingへ置き換えられます。
bool GenerateBoxBoxManifold(
    Entity boxEntityA,
    const TransformComponent& boxTransformA,
    const ColliderComponent& boxColliderA,
    Entity boxEntityB,
    const TransformComponent& boxTransformB,
    const ColliderComponent& boxColliderB,
    ContactManifold& outManifold);

} // namespace Raven::ph
