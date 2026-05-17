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

    uint32_t count = sizeof(indices) / sizeof(uint32_t);

    m_VertexArray = VertexArray::Create();

    auto vertexBuffer = VertexBuffer::Create(vertices, sizeof(vertices));

    auto indexBuffer = IndexBuffer::Create(indices, count);

    m_VertexArray->AddVertexBuffer(vertexBuffer);
    m_VertexArray->SetIndexBuffer(indexBuffer);

    m_Shader = Shader::Create("assets/shaders/FlatColor.glsl");
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
