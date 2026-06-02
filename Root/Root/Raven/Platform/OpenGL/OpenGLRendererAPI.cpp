#include "OpenGLRendererAPI.h"
#include "../../Renderer/Shader/Shader.h"
#include "../../Renderer/Texture/Texture.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>


namespace Raven
{

void OpenGLRendererAPI::Init()
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

#if 0

    float vertices[] =
    {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f
    };

    unsigned int vao;
    unsigned int vbo;

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        3 * sizeof(float),
        nullptr
    );

    const char* vertexShaderSource = R"(
#version 330 core

layout(location = 0) in vec3 a_Position;

void main()
{
    gl_Position = vec4(a_Position, 1.0);
}
)";

    const char* fragmentShaderSource = R"(
#version 330 core

out vec4 FragColor;

void main()
{
    FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
)";

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);

    unsigned int shaderProgram = glCreateProgram();

    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
#endif
}

void OpenGLRendererAPI::SetClearColor(float r, float g, float b, float a)
{
	glClearColor(r, g, b, a);
}

void OpenGLRendererAPI::Clear()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRendererAPI::DrawIndexed(const Ref<VertexArray>& vertexArray)
{
    vertexArray->Bind();
    glDrawElements(
        GL_TRIANGLES,
        vertexArray->GetIndexBuffer()->GetCount(),
        GL_UNSIGNED_INT,
        nullptr
    );
}

void OpenGLRendererAPI::BindShader(const std::shared_ptr<Shader>& shader)
{
    if (!shader) return;
    if (m_CurrentShader == shader) return;

    m_CurrentShader = shader;
    m_CurrentShader->Bind();
}

void OpenGLRendererAPI::BindTexture(const std::string& name, const std::shared_ptr<Texture>& texture, uint32_t slot)
{
    if (!texture || !m_CurrentShader) return;

    texture->Bind(slot);
    m_CurrentShader->SetInt(name, static_cast<int>(slot));
}

void OpenGLRendererAPI::UploadUniform(const std::string& name, const UniformValue& value)
{
    if (!m_CurrentShader) return;

    std::visit([&](const auto& v)
    {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, int>)
            m_CurrentShader->SetInt(name, v);

        else if constexpr (std::is_same_v<T, float>)
            m_CurrentShader->SetFloat(name, v);

        else if constexpr (std::is_same_v<T, math::Vec2>)
            m_CurrentShader->SetVec2(name, v);

        else if constexpr (std::is_same_v<T, math::Vec3>)
            m_CurrentShader->SetVec3(name, v);

        else if constexpr (std::is_same_v<T, math::Vec4>)
            m_CurrentShader->SetVec4(name, v);

        else if constexpr (std::is_same_v<T, math::Mat4>)
            m_CurrentShader->SetMat4(name, v);

    }, value);
}

}