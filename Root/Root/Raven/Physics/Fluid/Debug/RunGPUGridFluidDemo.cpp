#include "Raven/Physics/Fluid/Debug/RunGPUGridFluidDemo.h"
#include "Raven/Physics/Fluid/GPUGridFluid.h"
#include "Raven/Physics/Coupling/GridThermalCoupling.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLCompute.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHIDevice.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>

namespace Raven::ph
{
namespace
{
using math::Vec3;
constexpr float FixedStep = 1.0f / 60.0f;
constexpr float SphereRadius = 0.50f;
constexpr float ParticleRadius = 0.02f;
const DenseSDFGrid DemoGrid{{-2.05f, -2.05f, -0.25f}, {0.10f, 0.10f, 0.10f}, {41, 41, 5}};

bool Check(bool success, const char* description)
{
    if (success == false)
    {
        std::cerr << "[FAIL] GPU Grid Fluid: " << description << '\n';
    }
    return success;
}

std::string ReadShader(const char* name)
{
    std::ifstream file(std::string("Raven/Assets/Shaders/") + name, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool PrepareShaders(RHIComputePipelineSpecification& sdf, RHIComputePipelineSpecification& grid,
    RHIComputePipelineSpecification& particles)
{
    sdf.OpenGLSource = ReadShader("DenseSDF.comp");
    const auto common = ReadShader("DenseSDF.inc");
    const std::string include = "#include \"DenseSDF.inc\"";
    const auto marker = sdf.OpenGLSource.find(include);
    particles.OpenGLSource = ReadShader("GPUGridFluid.comp");
    const auto line = particles.OpenGLSource.find('\n');
    if (marker == std::string::npos || common.empty() || line == std::string::npos)
    {
        return false;
    }
    sdf.OpenGLSource.replace(marker, include.size(), common);
    grid.OpenGLSource = particles.OpenGLSource;
    grid.OpenGLSource.insert(line + 1, "#define RAVEN_GRID\n");
    return true;
}

class Simulation
{
public:
    bool Init(const Ref<RHIComputeCommandEncoder>& encoder, const DenseSDFSource& boundary,
        const GPUGridFluidSettings& settings, std::span<const Vec3> seeds)
    {
        RHIComputePipelineSpecification sdf, grid, particles;
        return PrepareShaders(sdf, grid, particles) && Boundary.Prepare(*encoder, sdf, boundary, DemoGrid) &&
            Boundary.RecordTexture(*encoder) && Solver.Prepare(encoder, grid, particles, Boundary, settings, seeds) &&
            World.RegisterSimulationParticipant(Solver) && World.GetCouplingBindingCount() == 0;
    }

    DenseSDFGenerator Boundary;
    GPUGridFluid Solver;
    // Worldの登録は非所有です。公開側を先に破棄し、Solverの寿命を保証します。
    FluidWorld World;
};

GLuint Compile(GLenum stage, const char* source)
{
    const GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE)
    {
        char log[1024]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << log << '\n';
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint MakeProgram(const char* vertex, const char* fragment)
{
    const GLuint vs = Compile(GL_VERTEX_SHADER, vertex);
    const GLuint fs = Compile(GL_FRAGMENT_SHADER, fragment);
    GLuint program = 0;
    if (vs != 0 && fs != 0)
    {
        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE)
        {
            glDeleteProgram(program);
            program = 0;
        }
    }
    if (vs != 0) { glDeleteShader(vs); }
    if (fs != 0) { glDeleteShader(fs); }
    return program;
}

class DemoRenderer
{
public:
    ~DemoRenderer()
    {
        glDeleteProgram(m_Field);
        glDeleteProgram(m_Particles);
        glDeleteVertexArrays(1, &m_VAO);
    }

    bool Init()
    {
        m_Field = MakeProgram(R"(#version 430 core
out vec2 uv;
void main()
{
    const vec2 points[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
    vec2 p = points[gl_VertexID];
    uv = (p + 1.0) * 0.5;
    gl_Position = vec4(p, 0, 1);
}
)", R"(#version 430 core
layout(binding=0) uniform sampler3D field;
in vec2 uv;
out vec4 color;
void main()
{
    float d = texture(field, vec3(uv, 0.5)).r;
    vec3 outside = vec3(0.04, 0.07, 0.12);
    vec3 inside = vec3(0.08, 0.31, 0.39);
    color = vec4(abs(d) < 0.042 ? vec3(0.93, 0.72, 0.28) : (d < 0.0 ? inside : outside), 1);
}
)");
        m_Particles = MakeProgram(R"(#version 430 core
layout(std430, binding=0) readonly buffer Particles { vec4 values[]; };
out float contacts;
void main()
{
    vec4 particle = values[gl_VertexID * 2];
    gl_Position = vec4(particle.xy / 2.05, 0, 1);
    gl_PointSize = 6.0;
    contacts = particle.w;
}
)", R"(#version 430 core
in float contacts;
out vec4 color;
void main()
{
    if (length(gl_PointCoord - 0.5) > 0.5) discard;
    color = vec4(contacts > 0 ? vec3(1, 0.63, 0.27) : vec3(0.31, 0.86, 0.99), 1);
}
)");
        glGenVertexArrays(1, &m_VAO);
        return m_Field != 0 && m_Particles != 0 && m_VAO != 0;
    }

    bool Draw(GLFWwindow& window, const Simulation& simulation, int count) const
    {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(&window, &width, &height);
        if (width == 0 || height == 0)
        {
            return true;
        }
        glViewport(0, 0, width, height);
        glBindVertexArray(m_VAO);
        glUseProgram(m_Field);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, GetOpenGLComputeTextureID(simulation.Boundary.GetTexture()));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glUseProgram(m_Particles);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, GetOpenGLComputeBufferID(simulation.Solver.GetParticleBuffer()));
        glEnable(GL_PROGRAM_POINT_SIZE);
        glDrawArrays(GL_POINTS, 0, count);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
        glUseProgram(0);
        return glGetError() == GL_NO_ERROR;
    }

private:
    GLuint m_Field = 0;
    GLuint m_Particles = 0;
    GLuint m_VAO = 0;
};

bool ReadParticles(Simulation& simulation, std::vector<GPUGridFluidParticle>& result)
{
    if (simulation.Solver.GetReadbackCount() != 0)
    {
        return false;
    }
    const auto ticket = simulation.Solver.RequestParticleReadback();
    if (ticket == nullptr)
    {
        return false;
    }
    std::vector<std::byte> bytes;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto status = ticket->Poll(bytes);
    while (status == RHIComputeReadbackStatus::Pending && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        status = ticket->Poll(bytes);
    }
    if (status != RHIComputeReadbackStatus::Ready || bytes.size() % sizeof(GPUGridFluidParticle) != 0)
    {
        return false;
    }
    result.resize(bytes.size() / sizeof(GPUGridFluidParticle));
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return simulation.Solver.GetReadbackCount() == 1;
}

std::optional<DenseSDFSource> MakeThinWall()
{
    const Vec3 vertices[] = {{-.03f,-.7f,-.15f}, {.03f,-.7f,-.15f}, {.03f,.7f,-.15f}, {-.03f,.7f,-.15f},
        {-.03f,-.7f,.15f}, {.03f,-.7f,.15f}, {.03f,.7f,.15f}, {-.03f,.7f,.15f}};
    const DenseSDFSource::Triangle indices[] = {{0,2,1}, {0,3,2}, {4,5,6}, {4,6,7},
        {0,1,5}, {0,5,4}, {1,2,6}, {1,6,5}, {2,3,7}, {2,7,6}, {3,0,4}, {3,4,7}};
    return DenseSDFSource::CreateMesh(vertices, indices, DenseSDFSource::Mode::ClosedMesh);
}

bool VerifyThinWall(const Ref<RHIComputeCommandEncoder>& encoder)
{
    const auto wall = MakeThinWall();
    Simulation simulation;
    const Vec3 seeds[] = {{-0.70f, 0, 0}};
    GPUGridFluidSettings settings;
    settings.InitialVelocity = {1000, 0, 0};
    settings.ParticleRadius = 0.015f;
    if (wall.has_value() == false || simulation.Init(encoder, *wall, settings, seeds) == false)
    {
        return false;
    }
    simulation.World.StepSimulation(FixedStep);
    std::vector<GPUGridFluidParticle> particles;
    if (ReadParticles(simulation, particles) == false || particles.size() != 1)
    {
        return false;
    }
    const auto& particle = particles.front();
    std::cout << "thinWall=0.06m, limited=0.80m, x=" << particle.PositionAndContacts[0]
        << ", contacts=" << particle.PositionAndContacts[3] << ", flags=" << particle.VelocityAndFlags[3] << '\n';
    return particle.PositionAndContacts[0] < -0.03f && particle.PositionAndContacts[3] == 1 &&
        particle.VelocityAndFlags[3] == 2 && std::abs(particle.VelocityAndFlags[0]) < 1.0e-4f;
}

bool VerifyUndefinedNormal(const Ref<RHIComputeCommandEncoder>& encoder, const DenseSDFSource& sphere)
{
    Simulation simulation;
    const Vec3 seeds[] = {{0, 0, 0}};
    GPUGridFluidSettings settings;
    if (simulation.Init(encoder, sphere, settings, seeds) == false)
    {
        return false;
    }
    simulation.World.StepSimulation(FixedStep);
    std::vector<GPUGridFluidParticle> particles;
    return ReadParticles(simulation, particles) && particles.size() == 1 &&
        particles[0].PositionAndContacts == std::array{0.0f, 0.0f, 0.0f, 0.0f} &&
        particles[0].VelocityAndFlags[3] == 1.0f;
}

bool Verify(GLFWwindow& window, const Ref<RHIComputeCommandEncoder>& encoder, const DemoRenderer& renderer,
    const DenseSDFSource& sphere, const GPUGridFluidSettings& settings, std::span<const Vec3> seeds)
{
    constexpr std::uint64_t steps = 90;
    std::vector<GPUGridFluidParticle> first;
    for (int replay = 0; replay < 2; ++replay)
    {
        Simulation simulation;
        if (Check(simulation.Init(encoder, sphere, settings, seeds), "initialize fixed-step simulation") == false)
        {
            return false;
        }
        simulation.World.StepSimulation(FixedStep * 2.0f);
        if (simulation.Solver.GetStepGeneration() != 0)
        {
            return false;
        }
        for (std::uint64_t index = 0; index < steps; ++index)
        {
            simulation.World.StepSimulation(FixedStep);
        }
        std::vector<GPUGridFluidParticle> result;
        if (Check(simulation.Solver.GetStepGeneration() == steps &&
            renderer.Draw(window, simulation, static_cast<int>(seeds.size())) &&
            ReadParticles(simulation, result) && result.size() == seeds.size(), "draw and checkpoint GPU particles") == false)
        {
            return false;
        }
        float maxPenetration = 0.0f;
        std::uint32_t contacts = 0;
        for (const auto& particle : result)
        {
            const auto& p = particle.PositionAndContacts;
            const float distance = sphere.Evaluate({p[0], p[1], p[2]});
            if (std::isfinite(distance) == false || particle.VelocityAndFlags[3] != 0.0f)
            {
                return false;
            }
            maxPenetration = std::max(maxPenetration, ParticleRadius - distance);
            contacts += static_cast<std::uint32_t>(p[3]);
        }
        std::cout << "generation=" << simulation.Solver.GetStepGeneration() << ", readbacks="
            << simulation.Solver.GetReadbackCount() << ", contacts=" << contacts
            << ", maxAnalyticPenetration=" << maxPenetration << "m\n";
        if (Check(contacts > 0 && maxPenetration <= 0.012f, "sphere collision tolerance") == false)
        {
            return false;
        }
        if (replay == 0)
        {
            first = std::move(result);
        }
        else if (Check(std::memcmp(first.data(), result.data(), result.size() * sizeof(GPUGridFluidParticle)) == 0,
            "bitwise fixed-step replay on the same GPU") == false)
        {
            return false;
        }
    }
    return Check(VerifyThinWall(encoder), "bounded high-speed sweep against thin SDF wall") &&
        Check(VerifyUndefinedNormal(encoder, sphere), "undefined normal reporting and stable position");
}

bool VerifyThermalVelocity(const Ref<RHIComputeCommandEncoder>& encoder, const DenseSDFSource& sphere,
    std::span<const Vec3> seeds)
{
    RHIComputePipelineSpecification sdf, grid, particles;
    DenseSDFGenerator boundary;
    auto fluid = std::make_shared<GPUGridFluid>();
    GPUGridFluidSettings settings;
    settings.InitialVelocity = { 1.2f, 0.0f, 0.0f };
    if (PrepareShaders(sdf, grid, particles) == false || boundary.Prepare(*encoder, sdf, sphere, DemoGrid) == false ||
        boundary.RecordTexture(*encoder) == false || fluid->Prepare(encoder, grid, particles, boundary, settings, seeds) == false)
    {
        return false;
    }

    const std::size_t hotCell = 5 + DemoGrid.Size[0] * (20 + DemoGrid.Size[1] * 2);
    std::vector<double> initial(DemoGrid.GetVoxelCount());
    initial[hotCell] = 100.0;
    auto temperature = GridTemperatureField::Create(DemoGrid, 1000.0, 0.0, initial);
    if (temperature.has_value() == false)
    {
        return false;
    }
    auto thermal = std::make_shared<GridThermalCoupling>(std::move(*temperature));
    thermal->SetGPUFluid(fluid);
    Scene scene;
    auto& world = scene.GetPhysicsSimulationWorld();
    if (world.GetFluidWorld().RegisterSimulationParticipant(*fluid) == false)
    {
        return false;
    }
    world.SetGridThermalCoupling(thermal);
    const double energy = thermal->GetField().GetTotalEnergy();
    const auto start = std::chrono::steady_clock::now();
    world.StepSimulation(scene, FixedStep);
    const auto queued = std::chrono::steady_clock::now();
    if (thermal->GetLastStatus() != GridThermalStepStatus::WaitingForVelocity ||
        thermal->GetField().GetTotalEnergy() != energy || fluid->GetVelocityReadbackCount() != 1 ||
        fluid->GetReadbackCount() != 0 || fluid->GetStepGeneration() != 1)
    {
        return false;
    }

    // テストだけが待機する。第1世代のコピーを第2世代のDispatch後に消費して所有と同期を確認する。
    while (fluid->GetCompletedVelocities().empty() && std::chrono::steady_clock::now() - start < std::chrono::seconds(5))
    {
        if (fluid->UpdateVelocityReadback() == false)
        {
            return false;
        }
        if (fluid->GetCompletedVelocities().empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const auto visible = std::chrono::steady_clock::now();
    world.StepSimulation(scene, FixedStep);
    const auto result = thermal->GetField().GetTemperatures();
    const double energyError = std::abs(thermal->GetField().GetTotalEnergy() - energy);
    std::cout << "Phase 09 GPU heat: step=" << fluid->GetStepGeneration()
        << ", consumed=" << thermal->GetUsedVelocityGeneration()
        << ", velocity readbacks=" << fluid->GetVelocityReadbackCount()
        << ", first fixed step CPU ms=" << std::chrono::duration<double, std::milli>(queued - start).count()
        << ", first snapshot ms=" << std::chrono::duration<double, std::milli>(visible - start).count()
        << ", downwind K=" << result[hotCell + 1] << ", energy error J=" << energyError << '\n';
    return thermal->GetLastStatus() == GridThermalStepStatus::Advanced &&
        thermal->GetUsedVelocityGeneration() == 1 && fluid->GetStepGeneration() == 2 &&
        fluid->GetVelocityReadbackCount() == 2 && fluid->GetReadbackCount() == 0 &&
        std::abs(result[hotCell + 1] - 20.0) < 1.0e-4 && energyError < 1.0e-9;
}

bool Run(GLFWwindow& window, bool verify)
{
    OpenGLRHIDevice device;
    const auto encoder = device.CreateComputeCommandEncoder();
    const auto sphere = DenseSDFSource::CreateSphere({}, SphereRadius);
    DemoRenderer renderer;
    if (Check(encoder != nullptr && sphere.has_value() && renderer.Init(), "OpenGL 4.3 pipelines") == false)
    {
        return false;
    }
    std::vector<Vec3> seeds;
    for (int y = -4; y <= 4; ++y)
    {
        for (int x = 0; x < 5; ++x)
        {
            seeds.emplace_back(-1.15f - 0.08f * static_cast<float>(x), 0.045f * static_cast<float>(y), 0);
        }
    }
    GPUGridFluidSettings settings;
    settings.Viscosity = 0.05f;
    settings.InitialVelocity = {1.2f, 0, 0};
    settings.Acceleration = {0.06f, 0, 0};
    settings.ParticleRadius = ParticleRadius;
    if (verify)
    {
        return Verify(window, encoder, renderer, *sphere, settings, seeds) &&
            Check(VerifyThermalVelocity(encoder, *sphere, seeds), "completed velocity snapshot and conservative heat advection");
    }
    Simulation simulation;
    if (simulation.Init(encoder, *sphere, settings, seeds) == false)
    {
        return false;
    }
    glfwSwapInterval(1);
    double previous = glfwGetTime();
    double accumulator = 0.0;
    while (glfwWindowShouldClose(&window) == GLFW_FALSE)
    {
        glfwPollEvents();
        if (glfwGetKey(&window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            break;
        }
        const double now = glfwGetTime();
        accumulator += std::min(now - previous, 0.10);
        previous = now;
        while (accumulator >= FixedStep)
        {
            simulation.World.StepSimulation(FixedStep);
            accumulator -= FixedStep;
        }
        if (simulation.Solver.IsReady() == false || renderer.Draw(window, simulation, static_cast<int>(seeds.size())) == false)
        {
            return false;
        }
        glfwSwapBuffers(&window);
    }
    return true;
}
} // namespace

int RunGPUGridFluidDemo(bool verify)
{
    if (glfwInit() != GLFW_TRUE)
    {
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, verify ? GLFW_FALSE : GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(640, 640, "Raven GPU Grid Fluid and SDF", nullptr, nullptr);
    bool successful = false;
    if (window != nullptr)
    {
        glfwMakeContextCurrent(window);
        successful = gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) != 0 && Run(*window, verify);
        glfwDestroyWindow(window);
    }
    glfwTerminate();
    if (verify && successful)
    {
        std::cout << "[PASS] GPU Grid Fluid / SDF integration\n";
    }
    return successful ? 0 : 1;
}
} // namespace Raven::ph
