#include "Raven/Platform/OpenGL/RHI/OpenGLCompute.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHIDevice.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace
{
using namespace Raven;

constexpr auto ComputeSource = R"(#version 430 core
layout(local_size_x = 8, local_size_y = 1, local_size_z = 1) in;
layout(std430, binding = 0) buffer Values { uint values[]; };
layout(r32f, binding = 1) uniform writeonly image3D volume;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i < uint(values.length())) {
        values[i] = values[i] * 3u + 7u;
    }
    if (i < 8u) {
        ivec3 p = ivec3(int(i & 1u), int((i >> 1u) & 1u), int(i >> 2u));
        imageStore(volume, p, vec4(float(i) / 8.0, 0, 0, 1));
    }
}
)";

bool Check(bool success, const char* description)
{
    if (success == false)
    {
        std::cerr << "[FAIL] " << description << '\n';
    }
    return success;
}

GLuint CompileDrawShader(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE)
    {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool VerifyDraw(const Ref<RHIComputeResource>& image)
{
    const GLuint vertex = CompileDrawShader(GL_VERTEX_SHADER, R"(#version 330 core
void main()
{
    const vec2 vertices[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
    gl_Position = vec4(vertices[gl_VertexID], 0, 1);
}
)");
    const GLuint fragment = CompileDrawShader(GL_FRAGMENT_SHADER, R"(#version 330 core
uniform sampler3D volume;
out vec4 color;
void main()
{
    int i = int(gl_FragCoord.x);
    vec3 uvw = (vec3(i & 1, (i >> 1) & 1, i >> 2) + 0.5) / 2.0;
    color = vec4(texture(volume, uvw).r, 0, 0, 1);
}
)");
    if (vertex == 0 || fragment == 0)
    {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return false;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE)
    {
        glDeleteProgram(program);
        return false;
    }
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glViewport(0, 0, 8, 1);
    glDisable(GL_DITHER);
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, GetOpenGLComputeTextureID(image));
    glUniform1i(glGetUniformLocation(program, "volume"), 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    std::array<std::uint8_t, 32> pixels{};
    glReadPixels(0, 0, 8, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_3D, 0);
    glUseProgram(0);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    for (int i = 0; i < 8; ++i)
    {
        const int expected = i * 255 / 8;
        if (std::abs(static_cast<int>(pixels[4 * i]) - expected) > 1 || pixels[4 * i + 3] != 255)
        {
            return false;
        }
    }
    return glGetError() == GL_NO_ERROR;
}

bool VerifyCompute()
{
    OpenGLRHIDevice device;
    const auto caps = device.QueryComputeCapabilities();
    auto encoder = device.CreateComputeCommandEncoder();
    if (Check(caps.Supported && encoder != nullptr, "OpenGL 4.3 Compute capabilities") == false)
    {
        return false;
    }
    RHIComputePipelineSpecification spec;
    spec.OpenGLSource = ComputeSource;
    spec.LocalSize = {8, 1, 1};
    spec.Layout = {{0, RHIComputeBindingType::StorageBuffer, RHIComputeAccess::ReadWrite},
        {1, RHIComputeBindingType::StorageImage3D, RHIComputeAccess::Write}};
    const auto pipeline = encoder->CreatePipeline(spec);
    // 端数を含む配列でWorkgroupの切り上げとShader境界チェックも検証します。
    std::array<std::uint32_t, 19> values{};
    for (std::uint32_t i = 0; i < values.size(); ++i)
    {
        values[i] = i;
    }
    const auto buffer = encoder->CreateStorageBuffer(sizeof(values), values.data());
    const auto image = encoder->CreateStorageImage3D({2, 2, 2});
    const RHIComputeBinding bindings[] = {{0, buffer}, {1, image}};
    if (Check(pipeline != nullptr && buffer != nullptr && image != nullptr, "GL Compute resources") == false ||
        Check(encoder->Dispatch(pipeline, bindings, {0, 1, 1}) == false, "zero Dispatch rejected") == false ||
        Check(encoder->RequestBufferReadback(buffer, sizeof(values), 1) == nullptr, "out-of-range copy rejected") == false ||
        Check(encoder->Dispatch(pipeline, bindings, {3, 1, 1}), "first Compute Dispatch") == false ||
        Check(encoder->Barrier(RHIComputeConsumer::Storage), "Compute-to-Compute barrier") == false ||
        Check(encoder->Dispatch(pipeline, bindings, {3, 1, 1}), "second Compute Dispatch") == false ||
        Check(encoder->Barrier(RHIComputeConsumer::TextureSampling), "Compute-to-Graphics barrier") == false ||
        Check(VerifyDraw(image), "3D Compute image sampled by fragment shader") == false ||
        Check(encoder->Barrier(RHIComputeConsumer::BufferReadback), "Compute-to-Readback barrier") == false)
    {
        return false;
    }
    const auto ticket = encoder->RequestBufferReadback(buffer, 0, sizeof(values));
    if (Check(ticket != nullptr, "queued GPU-to-CPU buffer copy") == false)
    {
        return false;
    }
    std::vector<std::byte> bytes;
    auto state = ticket->Poll(bytes);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (state == RHIComputeReadbackStatus::Pending && std::chrono::steady_clock::now() < deadline)
    {
        if (Check(bytes.empty(), "Pending Readback has no CPU payload") == false)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        state = ticket->Poll(bytes);
    }
    if (Check(state == RHIComputeReadbackStatus::Ready && bytes.size() == sizeof(values), "Readback completion") == false)
    {
        return false;
    }
    for (auto& value : values)
    {
        value = (value * 3u + 7u) * 3u + 7u;
    }
    return Check(std::memcmp(bytes.data(), values.data(), sizeof(values)) == 0, "CPU reference array equality");
}
} // namespace

bool RunOpenGLComputeGPUIntegrationTests()
{
    if (Check(glfwInit() == GLFW_TRUE, "GLFW initialization") == false)
    {
        return false;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Raven OpenGL Compute Smoke Test", nullptr, nullptr);
    bool passed = false;
    if (Check(window != nullptr, "OpenGL 4.3 Core Context") == true)
    {
        glfwMakeContextCurrent(window);
        const bool loaded = gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) != 0;
        if (Check(loaded, "GLAD initialization") == true)
        {
            std::cout << "OpenGL " << glGetString(GL_VERSION) << " / " << glGetString(GL_RENDERER) << '\n';
            // Compute Encoderと全ResourceをContext破棄前に解放します。
            passed = VerifyCompute();
        }
        glfwDestroyWindow(window);
    }
    glfwTerminate();
    if (passed == true)
    {
        std::cout << "[PASS] OpenGL Compute array / Readback / 3D Graphics sampling\n";
    }
    return passed;
}
