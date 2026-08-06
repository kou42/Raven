#pragma once

#include "Raven/Physics/Contact.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{

// ============================================================================
// GenerateSpherePlaneContact
// ============================================================================
// Sphere Colliderと無限Plane Colliderの接触情報を生成します。
//
// 戻り値
//   true  : SphereがPlaneへ接触または貫通しており、outContactが有効
//   false : 接触していない、またはCollider設定が不正
//
// Contactの法線規約
//   Contact::Normalは「AからBへ向く法線」です。
//   この関数ではAをSphere、BをPlaneとして格納するため、法線はSphereからPlaneへ
//   向く方向、すなわちPlaneNormalの反対方向になります。
bool GenerateSpherePlaneContact(
    Entity sphereEntity,
    const TransformComponent& sphereTransform,
    const ColliderComponent& sphereCollider,
    Entity planeEntity,
    const TransformComponent& planeTransform,
    const ColliderComponent& planeCollider,
    Contact& outContact
);

} // namespace Raven::ph
