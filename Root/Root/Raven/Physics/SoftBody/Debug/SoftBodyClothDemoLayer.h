#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{

class Application;
class Material;
class Mesh;
class MeshDeformationInstance;

// ============================================================================
// SoftBodyClothDemoLayer
// ============================================================================
// XPBD Clothの目視確認と、RigidBody Sphereとの最小Soft/Rigid連成を検証するLayerです。
//
// SceneGame本体へSoftBody固有コードを埋め込まず、Clothは
//   MeshRendererComponent + MeshDeformationComponent
// を持つ通常のScene Entityとして登録します。
// RigidBody Sphereも
//   MeshRendererComponent + RigidBodyComponent + ColliderComponent
// を持つ通常EntityとしてPhysicsWorldとScene描画の両方へ参加します。
//
// Rigid -> Soft Collider同期はPhysicsSimulationWorldのFixed Step境界へ移管します。
// Application LayerのOnUpdate()は、現在はSoft -> Rigid反作用の適用とDebug Snapshotだけを担当し、
// RigidBody TransformからSoftBody Colliderへの毎frame手動同期は行いません。
//
// 描画そのものはScene側へ統合するため、OnRender()は追加Passを持ちません。
class SoftBodyClothDemoLayer : public Layer
{
public:
    explicit SoftBodyClothDemoLayer(Application& application)
        : m_Application(application)
    {
    }

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float deltaTime) override;
    void OnRender() override;

private:
    Application& m_Application;

    Entity m_ClothEntity{};
    Entity m_RigidSphereEntity{};

    Ref<Mesh> m_ClothMesh;
    Ref<Mesh> m_CollisionSphereMesh;
    Ref<Material> m_ClothMaterial;
    Ref<Material> m_RigidSphereMaterial;

    // Deformerを直接所有せず、MeshDeformationInstanceのshared ownershipを保持します。
    // 必要なときだけGetDeformer()からSoftBodyClothDeformerへdowncastして連成情報を交換します。
    Ref<MeshDeformationInstance> m_ClothDeformationInstance;

    // ClothのMesh依存初期化後にSolver Collider Indexが確定してからBindingを1回だけ登録します。
    // OnDetachではこの状態を使ってPhysicsSimulationWorldから非所有参照を解除してからEntityを破棄します。
    bool m_RigidSoftSphereBindingRegistered = false;
};

} // namespace Raven
