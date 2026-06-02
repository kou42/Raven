#include "SandboxLayer.h"
#include "../Renderer.h"
#include "../RenderCommand.h"

namespace Raven
{

SandboxLayer::SandboxLayer()
{

}

void SandboxLayer::OnAttach()
{
#if 0
    float vertices[] =
    {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f
    };

    uint32_t indices[] =
    {
        0, 1, 2
    };
#endif

    float vertices[] =
    {
        // position           // color
        -0.5f, -0.5f, 0.0f,   1.0f, 0.0f, 0.0f, // 0 ¶‰º
         0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f, // 1 ‰E‰º
         0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f, // 2 ‰Eã
        -0.5f,  0.5f, 0.0f,   1.0f, 1.0f, 0.0f  // 3 ¶ã
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

    //m_Texture = Texture::Create("Raven/Assets/Images/test/mountain1.png");
    m_Texture = m_TextureLibrary.Load("Mountain", "Raven/Assets/Images/test/mountain1.png");

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

#if 0
    std::string path = "Raven/Assets/Shaders/Glsl/FlatColor.glsl";
    m_Shader = Shader::Create(path);
#else
    std::string vertPath = "Raven/Assets/Shaders/Vertex/test.vert";     //"D:/Engine/Root/Root/Raven/Assets/Shaders/Vertex/test.vert";
    std::string fragPath = "Raven/Assets/Shaders/Fragment/test.frag";  //"D:/Engine/Root/Root/Raven/Assets/Shaders/Fragment/test.frag";
    m_Shader = m_ShaderLibrary.Load("Test", vertPath, fragPath);
    //m_Shader = Shader::Create(vertPath, fragPath);
#endif
}

void SandboxLayer::OnUpdate()
{
    m_Shader->Bind();
    m_Texture->Bind();

    RenderCommand::Clear();

    Renderer::BeginScene();

    Renderer::Submit(m_Shader, m_VertexArray);

    Renderer::EndScene();

}

}
