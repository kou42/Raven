#include "Raven/Renderer/Layer/SandboxLayer.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/RenderCommand.h"

namespace Raven
{

SandboxLayer::SandboxLayer()
{

}

void SandboxLayer::OnAttach()
{
    // SandboxLayerはLegacy OpenGL Resource/RenderCommandを直接検証する互換Layerです。
    // Explicit BackendではScene/Debug/UIのRHI経路を使用し、このLayerはGPU Resourceを生成しません。
    if (GetRHIBackend() != RHIBackend::OpenGL)
    {
        return;
    }
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

    // Sandboxも通常のRHI描画経路と同じくPipelineを明示します。
    // Shaderだけを直接Bindすると、直前のPhysics Debug等が設定したLines topologyを
    // CommandListが保持したままDrawする可能性があるため、Triangle PipelineをLayer寿命で保持します。
    PipelineSpecification pipelineSpecification{};
    pipelineSpecification.Shader = m_Shader;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.DebugName = "Sandbox Pipeline";
    m_Pipeline = Pipeline::Create(pipelineSpecification);
}

void SandboxLayer::OnUpdate(float dt)
{
    (void)dt;
    if (GetRHIBackend() != RHIBackend::OpenGL)
    {
        return;
    }

    RenderCommand::Clear();

    Renderer::BeginScene();

    if (m_Pipeline != nullptr)
    {
        RenderCommand::BindPipeline(m_Pipeline);
        RenderCommand::BindTexture("u_Texture", m_Texture, 0);
        RenderCommand::DrawIndexed(m_VertexArray);
    }

    Renderer::EndScene();

}

}
