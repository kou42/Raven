#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{

class Application;
class Material;
class Mesh;
class Shader;

// ============================================================================
// SoftBodyClothDemoLayer
// ============================================================================
// XPBD Clothの最小目視確認用Layerです。
//
// SceneGame本体へSoftBody固有コードを埋め込まず、SceneにはMeshDeformationComponentだけを
// 登録します。更新は既存MeshDeformationSystem、追加描画だけをこのLayerが担当します。
class SoftBodyClothDemoLayer : public Layer
{
public:
    explicit SoftBodyClothDemoLayer(Application& application)
        : m_Application(application)
    {
    }

    void OnAttach() override;
    void OnDetach() override;
    void OnRender() override;

private:
    Application& m_Application;
    Entity m_ClothEntity{};
    Ref<Mesh> m_ClothMesh;
    Ref<Material> m_ClothMaterial;
};

} // namespace Raven
