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
        -0.5f, -0.5f, 0.0f,   1.0f, 0.0f, 0.0f, // 0 左下
         0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f, // 1 右下
         0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f, // 2 右上
        -0.5f,  0.5f, 0.0f,   1.0f, 1.0f, 0.0f  // 3 左上
    };

    uint32_t indices[] =
    {
        0, 1, 2,
        2, 3, 0
    };

    float vertices_texture[] =
    {
        // position           // color
        -0.5f, -0.5f, 0.0f,   1.0f, 0.0f, 0.0f, // 0 左下
         0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f, // 1 右下
         0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f, // 2 右上
        -0.5f,  0.5f, 0.0f,   1.0f, 1.0f, 0.0f  // 3 左上
    };

    uint32_t indices_texture[] =
    {
        0, 1, 2,
        2, 3, 0
    };

    uint32_t count = sizeof(indices) / sizeof(uint32_t);

    m_VertexArray = VertexArray::Create();

    auto vertexBuffer = VertexBuffer::Create(vertices, sizeof(vertices));

    auto indexBuffer = IndexBuffer::Create(indices, count);

    m_VertexArray->AddVertexBuffer(vertexBuffer);
    m_VertexArray->SetIndexBuffer(indexBuffer);

    m_Texture = Texture::Create("Raven/Assets/Images/test/mountain1.png");

    uint32_t count_texture = sizeof(indices_texture) / sizeof(uint32_t);
    auto vertexBuffer_texure = VertexBuffer::Create(vertices_texture, sizeof(indices_texture));
    auto indexBuffer_texture = IndexBuffer::Create(indices_texture, count_texture);
    m_VertexArray->AddVertexBuffer(vertexBuffer_texure);
    m_VertexArray->SetIndexBuffer(indexBuffer_texture);

#if 0
    std::string path = "Raven/Assets/Shaders/Glsl/FlatColor.glsl";
    m_Shader = Shader::Create(path);
#else
    std::string vertPath = "Raven/Assets/Shaders/Vertex/test.vert";     //"D:/Engine/Root/Root/Raven/Assets/Shaders/Vertex/test.vert";
    std::string fragPath = "Raven/Assets/Shaders/Fragment/test.frag";  //"D:/Engine/Root/Root/Raven/Assets/Shaders/Fragment/test.frag";
    m_Shader = Shader::Create(vertPath, fragPath);
#endif
}

void SandboxLayer::OnUpdate()
{
    m_Shader->Bind();

    RenderCommand::Clear();

    Renderer::BeginScene();

    Renderer::Submit(m_Shader, m_VertexArray);

    Renderer::EndScene();

}

}
