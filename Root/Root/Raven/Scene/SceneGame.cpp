#include "SceneGame.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/RenderCommand.h"
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
        -0.5f, -0.5f, 0.0f, 0, 0, 1,      0.0f, 0.0f,
        -0.5f,  0.5f, 0.0f, 1, 1, 0,      0.0f, 1.0f
    };

    uint32_t indices_texture[] =
    {
        0, 1, 2,
        2, 3, 0
    };

    uint32_t count = sizeof(indices) / sizeof(uint32_t);

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

    // Entity生成
    //m_Player = CreateEntity("Player");
    //m_Camera = CreateEntity("MainCamera");

    // Layer生成
    //PushLayer(CreateScope<GameLayer>());
    //PushLayer(CreateScope<UILayer>());
}

void SceneGame::OnDestroy()
{
    // 明示的に破棄したいものがあればここで行う
    m_layers.clear();

    m_VertexArray.reset();
    m_Texture.reset();
    m_Shader.reset();
}

void SceneGame::OnUpdate(float dt)
{
#if 0
    // 入力
    if (Input::IsKeyPressed(Key::W))
    {
        m_PlayerPosition.y += m_PlayerSpeed * dt;
    }

    // 物理更新
    m_PhysicsWorld.Step(dt);

    // Entity更新
    for (auto& entity : m_Entities)
    {
        entity.OnUpdate(dt);
    }

    // Layer更新
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

    Renderer::Submit(m_Shader, m_VertexArray);

    Renderer::EndScene();

    for (auto& layer : m_layers)
    {
        layer->OnRender();
    }
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
        // カメラのアスペクト比更新
        // RendererのViewport更新
    }
}

}
