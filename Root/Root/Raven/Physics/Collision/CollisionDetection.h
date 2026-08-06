#pragma once

#include "Raven/Physics/Contact.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{

// ============================================================================
// GenerateSphereSphereManifold
// ============================================================================
// 2つのSphere Colliderから、1接触点を持つContactManifoldを直接生成します。
//
// 戻り値
//   true  : Sphere同士が接触または貫通しており、outManifoldが有効
//   false : 接触していない、またはCollider設定が不正
//
// 法線規約
//   ContactManifold::Normalは「AからBへ向く法線」です。
//   中心が完全に一致して法線を一意に決められない場合は、安定した既定方向
//   (+X)を使用します。Manifold永続化後は前フレームの法線を再利用できます。
bool GenerateSphereSphereManifold(
    Entity sphereEntityA,
    const TransformComponent& sphereTransformA,
    const ColliderComponent& sphereColliderA,
    Entity sphereEntityB,
    const TransformComponent& sphereTransformB,
    const ColliderComponent& sphereColliderB,
    ContactManifold& outManifold
);

// ============================================================================
// GenerateSpherePlaneManifold
// ============================================================================
// Sphere Colliderと無限Plane Colliderから、1接触点を持つContactManifoldを
// 直接生成します。
//
// 戻り値
//   true  : SphereがPlaneへ接触または貫通しており、outManifoldが有効
//   false : 接触していない、またはCollider設定が不正
//
// 法線規約
//   この関数ではAをSphere、BをPlaneとして格納します。
//   したがってNormalはSphereからPlaneへ向く方向、つまり-planeNormalです。
bool GenerateSpherePlaneManifold(
    Entity sphereEntity,
    const TransformComponent& sphereTransform,
    const ColliderComponent& sphereCollider,
    Entity planeEntity,
    const TransformComponent& planeTransform,
    const ColliderComponent& planeCollider,
    ContactManifold& outManifold
);

} // namespace Raven::ph
