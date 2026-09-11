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
//   MeshRendererComponent + MeshDeformationComponent + RigidSoftCouplingComponent
// を持つ通常のScene Entityとして登録します。
// RigidBody Sphereも
//   MeshRendererComponent + RigidBodyComponent + ColliderComponent
// を持つ通常EntityとしてPhysicsWorldとScene描画の両方へ参加します。
//
// 双方向CouplingのBinding構築はMeshDeformationSystem、毎Fixed Step処理は
// PhysicsSimulationWorldへ集約します。Application Layerは永続設定となるComponentを
// OnAttach()で構成するだけで、Runtime BindingのRegister/Unregisterや毎frameのデータ交換を行いません。
// OnUpdate()はDebug Snapshotだけを担当します。
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
    // OnUpdate()のBrowser Debug Snapshotでだけ具体的なSoftBodyClothDeformerへdowncastします。
    // Coupling設定・Runtime Binding lifetimeはLayer外へ移管済みです。
    Ref<MeshDeformationInstance> m_ClothDeformationInstance;
};

} // namespace Raven
