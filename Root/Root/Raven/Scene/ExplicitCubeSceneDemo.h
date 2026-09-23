#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneCamera.h"

#include <GLFW/glfw3.h>

namespace Raven
{

// Explicit Backendの検証用SceneをBackend非依存で共有します。
// GPU Resourceの生成だけはRuntimeのPrepareSceneへ委譲し、ここではCPU Geometryを構築します。
class ExplicitCubeSceneDemo final
{
public:
    bool Init(const char* entityPrefix, uint32_t width, uint32_t height)
    {
        m_Scene = CreateScope<Scene>();
        m_Mesh = PrimitiveMeshFactory::CreateCube(LegacyMeshResourceCreation::Deferred);
        m_Material = CreateRef<Material>();
        if (m_Mesh == nullptr || m_Mesh->GetVertexArray() != nullptr ||
            m_Material == nullptr || m_Material->HasLegacyPipeline() == true)
        {
            Shutdown();
            return false;
        }
        m_Material->SetRHITint({0.35f, 0.75f, 1.0f, 1.0f});
        m_Material->SetSurfaceType(MaterialSurfaceType::Opaque);

        // Scene内のEntityは同じMeshとMaterialを共有します。
        m_Left = m_Scene->CreateEntity(std::string(entityPrefix) + "LeftCube");
        m_Left.GetComponent<TransformComponent>().Position = {-1.2f, 0.0f, 0.0f};
        m_Left.AddComponent<MeshRendererComponent>(MeshRendererComponent{m_Mesh, m_Material});
        m_Center = m_Scene->CreateEntity(std::string(entityPrefix) + "CenterCube");
        m_Center.AddComponent<MeshRendererComponent>(MeshRendererComponent{m_Mesh, m_Material});
        m_Right = m_Scene->CreateEntity(std::string(entityPrefix) + "RightCube");
        m_Right.GetComponent<TransformComponent>().Position = {1.2f, 0.0f, 0.0f};
        m_Right.AddComponent<MeshRendererComponent>(MeshRendererComponent{m_Mesh, m_Material});

        m_Camera.SetViewMatrix(math::Mat4::LookAt(
            {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}));
        ResizeCamera(width, height);
        return true;
    }

    Scene& GetScene() { return *m_Scene; }

    void ResizeCamera(uint32_t width, uint32_t height)
    {
        m_Camera.SetViewportSize(static_cast<float>(width), static_cast<float>(height));
    }

    void Render()
    {
        Renderer::BeginScene(m_Camera);
        const float time = static_cast<float>(glfwGetTime());
        m_Center.GetComponent<TransformComponent>().Rotation.y = time * 0.6f;
        m_Left.GetComponent<TransformComponent>().Rotation.x = -time * 0.35f;
        m_Right.GetComponent<TransformComponent>().Rotation.y = -time * 0.4f;
        m_Scene->RenderEntities();
    }

    void Shutdown()
    {
        // Sceneが持つEntity参照を先に外し、RuntimeのDeviceより前にMeshを解放します。
        m_Scene.reset();
        m_Mesh.reset();
        m_Material.reset();
        m_Left = Entity{};
        m_Center = Entity{};
        m_Right = Entity{};
    }

private:
    Scope<Scene> m_Scene;
    Ref<Mesh> m_Mesh;
    Ref<Material> m_Material;
    SceneCamera m_Camera;
    Entity m_Left;
    Entity m_Center;
    Entity m_Right;
};

} // namespace Raven
