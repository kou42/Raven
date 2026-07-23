#include "SceneGame.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"

namespace Raven
{

void SceneGame::OnCreate()
{

    float vertices[] =
    {
        // position           // color
        -0.5f, -0.5f, 0.0f,   1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f,
         0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f,
        -0.5f,  0.5f, 0.0f,   1.0f, 1.0f, 0.0f
    };

    uint32_t indices[] =
    {
        0, 1, 2,
        2, 3, 0
    };

    float vertices_texture[] =
    {
        // pos              // color        // uv
        0.5f,  0.5f, 0.0f, 1, 0, 0,      1.0f, 1.0f,
        0.5f, -0.5f, 0.0f, 0, 1, 0,      1.0f, 0.0f,
        -0.5f, -0.5f, 0.0f, 0, 0, 1,     0.0f, 0.0f,
        -0.5f,  0.5f, 0.0f, 1, 1, 0,     0.0f, 1.0f
    };

    uint32_t indices_texture[] =
    {
        0, 1, 2,
        2, 3, 0
    };

    m_VertexArray = VertexArray::Create();

    uint32_t count_texture = sizeof(indices_texture) / sizeof(uint32_t);
    auto vertexBuffer_texure = VertexBuffer::Create(vertices_texture, sizeof(vertices_texture));
    vertexBuffer_texure->SetLayout({
        { ShaderDataType::Float3, "a_Position" },
        { ShaderDataType::Float3, "a_Color" },
        { ShaderDataType::Float2, "a_Texcord" }
    });

    auto indexBuffer_texture = IndexBuffer::Create(indices_texture, count_texture);
    m_VertexArray->AddVertexBuffer(vertexBuffer_texure);
    m_VertexArray->SetIndexBuffer(indexBuffer_texture);

    m_Shader = m_ShaderLibrary.Load(
        "Test",
        "Raven/Assets/Shaders/Vertex/test.vert",
        "Raven/Assets/Shaders/Fragment/test.frag"
    );

    m_Texture = m_TextureLibrary.Load(
        "Mountain",
        "Raven/Assets/Images/test/mountain1.png"
    );

    PipelineSpecification pipelineSpecification;
    pipelineSpecification.DebugName = "SceneGame Quad Pipeline";
    pipelineSpecification.Shader = m_Shader;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.DepthCompare = DepthCompareOperator::Less;
    pipelineSpecification.Blend = true;

    Ref<Pipeline> pipeline = Pipeline::Create(pipelineSpecification);

    m_Material = CreateRef<Material>(pipeline);
    m_Mesh = CreateRef<Mesh>(m_VertexArray, static_cast<int32_t>(count_texture));

    m_SpawnedEntities.clear();

    const float xOffsets[] = { -0.6f, 0.0f, 0.6f };
    for (size_t i = 0; i < std::size(xOffsets); ++i)
    {
        Entity entity = CreateEntity("Quad_" + std::to_string(i));

        TransformComponent& transform = entity.GetComponent<TransformComponent>();
        transform.Position = { xOffsets[i], 0.0f, 0.0f };
        transform.Scale = { 0.45f, 0.45f, 1.0f };

        entity.AddComponent<MeshRendererComponent>(MeshRendererComponent{ m_Mesh, m_Material });
        m_SpawnedEntities.push_back(entity);
    }

#if 0
    // Entity設定
    m_Player = Scnene::CreateEntity("Player");
    m_Camera = Scnene::CreateEntity("MainCamera");

    // Layer設定
    Scnene::PushLayer(CreateScope<GameLayer>());
    PushLayer(CreateScope<UILayer>());
#endif

    // マテリアル設定
#if 0
    auto material = std::make_shared<Material>(shader);

    material->SetTexture("uTexture", texture, 0);
    material->Set("uColor", math::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
    material->Set("uRoughness", 0.5f);
#endif
}

void SceneGame::OnDestroy()
{
    // ここでは、シーンのリソースを解放する処理を行います。例えば、シェーダーやテクスチャ、頂点配列などのリソースを解放する必要があります。また、レイヤーもクリアしておくと良いでしょう。
    m_layers.clear();

    for (Entity entity : m_SpawnedEntities)
    {
        DestroyEntity(entity);
    }
    m_SpawnedEntities.clear();

    m_Mesh.reset();
    m_Material.reset();
    m_VertexArray.reset();
    m_Texture.reset();
    m_Shader.reset();
}

void SceneGame::OnUpdate(float dt)
{
#if 0
    // プレイヤーの移動処理
    if (Input::IsKeyPressed(Key::W))
    {
        m_PlayerPosition.y += m_PlayerSpeed * dt;
    }

    // カメラの移動処理
    m_PhysicsWorld.Step(dt);

    // Entityの更新処理
    for (auto& entity : m_Entities)
    {
        entity.OnUpdate(dt);
    }

    // レイヤーの更新処理
    for (auto& layer : m_layers)
    {
        layer->OnUpdate(dt);
    }
#endif
}

void SceneGame::OnRender()
{
    RenderCommand::SetClearColor(0.1f, 0.1f, 0.3f, 1.0f);
    RenderCommand::Clear();

    Renderer::BeginScene();
    Scene::RenderEntities();
    Renderer::EndScene();

    for (auto& layer : m_layers)
    {
        layer->OnRender();
    }
#if 0
    RenderCommand::SetClearColor(
        math::Vec4{ 0.1f, 0.1f, 0.1f, 1.0f }
    );

    RenderCommand::Clear();

    for (const auto& [id, meshRenderer] : scene.GetMeshRenderers())
    {
        if (meshRenderer.IsValid() == false) {
            continue;
        }

        const auto& transform =scene.GetComponent<TransformComponent>(id);

        Renderer::Draw(meshRenderer.Mesh, meshRenderer.Material, transform.GetTransform());
    }
#endif
}

void SceneGame::OnEvent(Event& e)
{
    for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it)
    {
        (*it)->OnEvent(e);

        if (e.Handled) {
            break;
        }
    }

    if (e.GetEventType() == EventType::WindowResize)
    {
        auto& resizeEvent = static_cast<WindowResizeEvent&>(e);
        RenderCommand::SetViewport(0, 0, resizeEvent.GetWidth(), resizeEvent.GetHeight());
    }
}

}
