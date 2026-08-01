#include "Application.h"
#include "../Renderer/Renderer.h"
//#include "../Core/Event.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace Raven
{

Application::Application()
{
    m_Window = Window::Create();

    m_Window->SetEventCallback([this](Event& event)
        {
            OnEvent(event);
        });

    Renderer::Init();
}

void Application::PushLayer(Layer* layer)
{
#if 0
    m_Layers.push_back(layer);

    layer->OnAttach();
#endif

}

void Application::PushLayer(Scope<Layer> layer)
{
    layer->OnAttach();
    // unique_ptrなので、所有権を移動させる必要がある
    m_Layers.push_back(std::move(layer));
}

void Application::SetScene(Scope<Scene> scene)
{
    if (m_scene) {
        m_scene->OnDestroy();
    }

    m_scene = std::move(scene);
    m_scene->OnCreate(); // ここで初期化
}

void Application::Run()
{

    float dt = 1.0f / 60.f;

    while (m_Running)
    {
        if (Input::IsKeyPressed(Key::Escape))
        {
            m_Running = false;
        }

#if 0
        if (Input::IsKeyPressed(Key::W))
        {
            std::cout << "W Pressed\n";
        }

        auto [x, y] = Input::GetMousePosition();
#endif

        m_scene->OnUpdate(dt);
        m_scene->OnRender();

        m_Window->OnUpdate();
    }
}

void Application::OnEvent(Event& event)
{
    std::cout << event.ToString() << std::endl;

    if (event.GetEventType() == EventType::WindowClose)
    {
        m_Running = false;
        event.Handled = true;
    }
}


#if 0
float vertices[] =
{
    // position          // color
    -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,
     0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,
     0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f
};

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

uint32_t vao;
uint32_t vbo;
uint32_t ebo;

glGenVertexArrays(1, &vao);
glBindVertexArray(vao);

// Vertex Buffer
glGenBuffers(1, &vbo);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

// Index Buffer
glGenBuffers(1, &ebo);
glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

// position
glEnableVertexAttribArray(0);

glVertexAttribPointer(
    0,
    3,
    GL_FLOAT,
    GL_FALSE,
    6 * sizeof(float),
    nullptr
);

// color
glEnableVertexAttribArray(1);
glVertexAttribPointer(
    1,
    3,
    GL_FLOAT,
    GL_FALSE,
    6 * sizeof(float),
    (const void*)(3 * sizeof(float))
);

const char* vertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Color;

out vec3 v_Color;

void main()
{
    v_Color = a_Color;
    gl_Position = vec4(a_Position, 1.0);
})";

const char* fragmentShaderSource = R"(
#version 330 core

in vec3 v_Color;

out vec4 FragColor;

void main()
{
    FragColor = vec4(v_Color, 1.0);
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
