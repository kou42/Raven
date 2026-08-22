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
// SoftBodyJellyDemoLayer
// ============================================================================
// XPBD Volume Constraintで構築したJellyをScene上で目視確認するためのDebug Layerです。
//
// Jelly固有のSimulation / Surface生成はSoftBodyJellyMeshDeformerへ閉じ込め、
// Scene側には通常の
//   TransformComponent
//   MeshRendererComponent
//   MeshDeformationComponent
// を持つEntityとして登録します。
//
// これによりClothとJellyが同じMeshDeformationSystem更新経路を共有し、
// Game View / Scene Viewでも通常Meshと同じ描画経路へ参加できます。
class SoftBodyJellyDemoLayer final : public Layer
{
public:
    explicit SoftBodyJellyDemoLayer(Application& application)
        : m_Application(application)
    {
    }

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float deltaTime) override;
    void OnRender() override;

private:
    Application& m_Application;

    Entity m_JellyEntity{};
    Ref<Mesh> m_JellyMesh;
    Ref<Material> m_JellyMaterial;
    Ref<MeshDeformationInstance> m_JellyDeformationInstance;
};

} // namespace Raven
