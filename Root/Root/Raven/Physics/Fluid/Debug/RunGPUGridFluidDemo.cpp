#include "Raven/Physics/Fluid/Debug/RunGPUGridFluidDemo.h"
#include "Raven/Physics/Fluid/GPUGridFluid.h"
#include "Raven/Physics/Fluid/GPUSPH.h"
#include "Raven/Physics/Fluid/SPHSolver.h"
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
#include <limits>
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

class SPHPointRenderer
{
public:
    ~SPHPointRenderer()
    {
        glDeleteProgram(m_Program);
        glDeleteVertexArrays(1, &m_VertexArray);
    }

    bool Init()
    {
        m_Program = MakeProgram(R"(#version 430 core
layout(std430, binding=0) readonly buffer Particles { vec4 values[]; };
uniform vec2 projection;
out float speed;
void main()
{
    uint index = uint(gl_VertexID) * 2u;
    vec3 position = values[index].xyz;
    speed = length(values[index + 1u].xyz);
    gl_Position = vec4(position.xy * projection, position.z * 0.4, 1);
    gl_PointSize = 8.0;
}
)", R"(#version 430 core
in float speed;
out vec4 color;
void main()
{
    float radial = length(gl_PointCoord - vec2(0.5)) * 2.0;
    if (radial > 1.0) discard;
    vec3 water = mix(vec3(0.13, 0.54, 0.85), vec3(0.42, 0.93, 1.0), min(speed * 0.2, 1.0));
    color = vec4(water * (1.0 - 0.38 * radial), 1);
}
)");
        if (m_Program == 0u)
        {
            return false;
        }
        m_ProjectionLocation = glGetUniformLocation(m_Program, "projection");
        glGenVertexArrays(1, &m_VertexArray);
        return m_VertexArray != 0u && m_ProjectionLocation >= 0;
    }

    bool Draw(const GPUSPH& solver, int width, int height) const
    {
        if (width <= 0 || height <= 0 || solver.GetParticleCount() == 0u)
        {
            return true;
        }
        const auto buffer = solver.GetParticleBuffer();
        const GLuint id = GetOpenGLComputeBufferID(buffer);
        if (id == 0u)
        {
            return false;
        }

        const float side = static_cast<float>(std::min(width, height)) * 0.9f;
        glViewport(0, 0, width, height);
        glClearColor(0.025f, 0.038f, 0.065f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glBindVertexArray(m_VertexArray);
        glUseProgram(m_Program);
        glUniform2f(m_ProjectionLocation, side / width, side / height);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, id);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(solver.GetParticleCount()));
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
        glUseProgram(0);
        glBindVertexArray(0);
        glDisable(GL_PROGRAM_POINT_SIZE);
        glDisable(GL_DEPTH_TEST);
        return glGetError() == GL_NO_ERROR;
    }

private:
    GLuint m_Program = 0u;
    GLuint m_VertexArray = 0u;
    GLint m_ProjectionLocation = -1;
};

bool VerifySPHResidentRender(GLFWwindow& window, RHIComputeCommandEncoder& encoder,
    GPUSPH& solver, const SPHPointRenderer& renderer)
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(&window, &width, &height);
    if (width < 64 || height < 64)
    {
        return false;
    }
    const auto before = encoder.GetStatistics();
    const auto particleBuffer = solver.GetParticleBuffer();
    GLuint query = 0u;
    glGenQueries(1, &query);
    if (query == 0u)
    {
        return false;
    }

    const auto verifyRegion = [&](int x, int regionWidth, bool expectsSamples)
    {
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, 0, regionWidth, height);
        glBeginQuery(GL_ANY_SAMPLES_PASSED, query);
        const bool drawn = renderer.Draw(solver, width, height);
        glEndQuery(GL_ANY_SAMPLES_PASSED);
        glDisable(GL_SCISSOR_TEST);
        GLuint samples = 0u;
        // 検証では描画クエリの真偽だけを取得し、画像・粒子バッファをCPUへコピーしません。
        glGetQueryObjectuiv(query, GL_QUERY_RESULT, &samples);
        return drawn && glGetError() == GL_NO_ERROR && (samples != 0u) == expectsSamples;
    };

    const int regionWidth = width / 3;
    const int right = width - regionWidth;
    // 中央の静止粒子で、別インデックスでもSSBOのストライドが正しいことを確認します。
    bool valid = verifyRegion(0, regionWidth, true) && verifyRegion(regionWidth, regionWidth, true)
        && verifyRegion(right, regionWidth, false);
    constexpr uint32_t frameCount = 12u;
    for (uint32_t frame = 0u; valid && frame < frameCount; ++frame)
    {
        // 前回の頂点シェーダーによるSSBO参照と、今回のCompute書き込みも同じContextで順序付けます。
        valid = encoder.Barrier(RHIComputeConsumer::Storage) && solver.DispatchFixedStep()
            && renderer.Draw(solver, width, height);
    }
    valid = valid && verifyRegion(0, regionWidth, false) && verifyRegion(regionWidth, regionWidth, true)
        && verifyRegion(right, regionWidth, true);
    glDeleteQueries(1, &query);
    const auto after = encoder.GetStatistics();
    valid = valid && solver.GetParticleBuffer() == particleBuffer
        && after.Dispatches == before.Dispatches + 5u * frameCount
        && after.Buffers == before.Buffers && after.Readbacks == before.Readbacks
        && after.ReadbackBytes == before.ReadbackBytes;
    if (valid == true)
    {
        std::cout << "OpenGL: " << glGetString(GL_RENDERER) << '\n'
            << "SPH render frames=" << frameCount << ", particles=" << solver.GetParticleCount() << ", dispatches=" << after.Dispatches - before.Dispatches
            << ", particle_readbacks=0, reallocations=0, region_transition=left_to_right\n";
    }
    return valid;
}

bool RunSPHResidentRender(GLFWwindow& window, bool verify)
{
    OpenGLRHIDevice device;
    const auto encoder = device.CreateComputeCommandEncoder();
    const auto capabilities = device.QueryComputeCapabilities();
    GPUSPH solver;
    SPHPointRenderer renderer;
    SPHSettings settings;
    settings.StableTimeStepEnabled = false;
    settings.BoundaryEnabled = true;
    settings.BoundaryRestitution = 0.4f;

    const float dt = verify ? 0.025f : 1.0f / 240.0f;
    std::vector<FluidParticle> particles;
    if (verify == true)
    {
        settings.Gravity = {};
        particles.resize(2u);
        particles[0].Position.x = -0.65f;
        particles[0].Velocity.x = 4.5f;
        particles[1].Position.y = 0.6f;
    }
    else
    {
        constexpr float spacing = 0.055f;
        for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 15; ++y)
        for (int x = 0; x < 15; ++x)
        {
            FluidParticle particle;
            particle.Position = {-0.55f + x * spacing, -0.10f + y * spacing, -0.09f + z * spacing};
            particle.Mass = settings.RestDensity * spacing * spacing * spacing;
            particles.push_back(particle);
        }
    }
    if (Check(solver.PrepareFixedStep(encoder, capabilities, particles, settings, dt, ReadShader("GPUSPH.comp"))
        == GPUSPH::PrepareResult::Ready && renderer.Init(), "SPH resident render setup") == false)
    {
        return false;
    }

    if (verify == true)
    {
        return Check(VerifySPHResidentRender(window, *encoder, solver, renderer), "SPH resident render verification");
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
        accumulator += std::min(now - previous, 1.0 / 30.0);
        previous = now;
        if (accumulator >= dt && encoder->Barrier(RHIComputeConsumer::Storage) == false)
        {
            return false;
        }
        while (accumulator >= dt)
        {
            if (solver.DispatchFixedStep() == false)
            {
                return false;
            }
            accumulator -= dt;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(&window, &width, &height);
        if (renderer.Draw(solver, width, height) == false)
        {
            return false;
        }
        glfwSwapBuffers(&window);
    }
    return true;
}

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

bool ReadSPHBuffer(const Ref<RHIComputeReadback>& ticket, std::vector<std::byte>& bytes)
{
    if (ticket == nullptr)
    {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto status = ticket->Poll(bytes);
    while (status == RHIComputeReadbackStatus::Pending && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        status = ticket->Poll(bytes);
    }
    return status == RHIComputeReadbackStatus::Ready;
}

bool VerifySPHFixedSteps(const Ref<RHIComputeCommandEncoder>& encoder, const RHIComputeCapabilities& capabilities,
    const std::string& source)
{
    SPHSettings settings;
    settings.StableTimeStepEnabled = false;
    settings.BoundaryEnabled = true;
    settings.BoundaryRestitution = 0.5f;
    constexpr float dt = 0.0005f;
    SPHSolver cpu(settings);
    GPUSPH gpu;

    std::vector<FluidParticle> initial(1024u);
    for (std::size_t index = 0u; index < initial.size(); ++index)
    {
        initial[index].Position = {static_cast<float>(index % 12u) * 0.065f - 0.4f,
            static_cast<float>((index / 12u) % 12u) * 0.065f - 0.4f,
            static_cast<float>(index / 144u) * 0.065f - 0.4f};
        initial[index].Velocity.y = static_cast<float>(index % 5u) * 0.04f;
        initial[index].Mass = 0.25f;
    }
    initial[0].Position = {0.979f, 0.0f, 0.0f};
    initial[0].Velocity.x = 5.0f;
    initial[1].Position = {1.1f, 0.0f, 0.0f};
    initial[1].Velocity.x = 1.0f;
    initial[1].Mass = -1.0f;

    using Result = GPUSPH::PrepareResult;
    for (const std::size_t stepCount : {1u, 12u})
    {
        if (Check(gpu.PrepareFixedStep(encoder, capabilities, initial, settings, dt, source)
            == Result::Ready, "SPH fixed step prepare") == false)
        {
            return false;
        }
        const auto prior = encoder->GetStatistics();
        const auto uploadCount = prior.Buffers;
        std::vector<Ref<RHIComputeTimestamp>> timestamps(stepCount);
        const auto gpuStart = std::chrono::steady_clock::now();
        for (auto& timestamp : timestamps)
        {
            if (Check(gpu.DispatchFixedStep(timestamp), "SPH fixed step dispatch") == false)
            {
                return false;
            }
        }
        glFinish();
        const double wallMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - gpuStart).count() / stepCount;
        const auto after = encoder->GetStatistics();
        if (Check(after.Dispatches == prior.Dispatches + stepCount * 5u
            && after.Buffers == uploadCount && after.Readbacks == prior.Readbacks,
            "SPH resident dispatches") == false)
        {
            return false;
        }
        const auto tickets = gpu.RequestStepReadback();
        std::vector<std::byte> stateBytes;
        std::vector<std::byte> dynamicsBytes;
        std::vector<FluidParticle> actual(initial.size());
        if (Check(ReadSPHBuffer(tickets.m_Particles, stateBytes) && ReadSPHBuffer(tickets.m_Dynamics, dynamicsBytes)
            && GPUSPH::DecodeStep(stateBytes, dynamicsBytes, actual)
            && encoder->GetStatistics().Readbacks == prior.Readbacks + 2u, "SPH final state readback") == false)
        {
            return false;
        }

        auto expected = initial;
        const auto cpuStart = std::chrono::steady_clock::now();
        for (std::size_t index = 0u; index < stepCount; ++index)
        {
            cpu.Step(expected, dt);
        }
        const double cpuMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - cpuStart).count() / stepCount;
        double gpuNs = 0.0;
        bool hasTimestamps = true;
        for (const auto& timestamp : timestamps)
        {
            double value = 0.0;
            if (timestamp == nullptr || timestamp->Poll(value) != RHIComputeReadbackStatus::Ready)
            {
                hasTimestamps = false;
                break;
            }
            gpuNs += value;
        }
        const double gpuMs = hasTimestamps ? gpuNs / (1.0e6 * stepCount) : wallMs;

        std::array<float, 5> maxErrors{};
        const auto error = [](float value, float reference, float scale = 1.0f)
        {
            return std::isfinite(value) && std::isfinite(reference) ? std::abs(value - reference) / std::max(scale, std::abs(reference))
                : std::numeric_limits<float>::infinity();
        };
        const auto vectorError = [&](const Vec3& value, const Vec3& reference)
        {
            return std::max({error(value.x, reference.x), error(value.y, reference.y), error(value.z, reference.z)});
        };
        // EOSの静止密度に相当する圧力スケールで比較し、ゼロ付近の除算による増幅を避けます。
        const float pressureScale = settings.RestDensity * settings.PressureStiffness;
        bool hasMatchedStates = true;
        for (std::size_t index = 0u; index < actual.size(); ++index)
        {
            const auto& current = actual[index];
            const auto& reference = expected[index];
            const std::array errors{error(current.Density, reference.Density),
                error(current.Pressure, reference.Pressure, pressureScale),
                vectorError(current.Force, reference.Force), vectorError(current.Position, reference.Position),
                vectorError(current.Velocity, reference.Velocity)};
            constexpr std::array tolerances{2.0e-4f, 2.0e-6f, 2.0e-3f, 5.0e-4f, 5.0e-4f};
            for (std::size_t field = 0u; field < errors.size(); ++field)
            {
                if (errors[field] > tolerances[field] || current.Mass != reference.Mass)
                {
                    if (hasMatchedStates == true)
                    {
                        std::cerr << "SPH substeps=" << stepCount << " particle=" << index
                            << " field=" << field << " normalized_error=" << errors[field] << '\n';
                    }
                    hasMatchedStates = false;
                }
                maxErrors[field] = std::max(maxErrors[field], errors[field]);
            }
        }
        if (Check(actual[0].Velocity.x < 0.0f && actual[1].Position.x == expected[1].Position.x
            && actual[1].Velocity.x == -0.5f, "SPH reflected and invalid-mass particles") == false)
        {
            return false;
        }
        std::cout << "fixed_steps=" << stepCount << ", cpu_ms=" << cpuMs << ", gpu_ms=" << gpuMs
            << ", gpu_timer=" << (hasTimestamps ? "timestamp" : "wall")
            << ", density_error=" << maxErrors[0] << ", pressure_error=" << maxErrors[1]
            << ", force_error=" << maxErrors[2] << ", position_error=" << maxErrors[3]
            << ", velocity_error=" << maxErrors[4] << ", readbacks=2\n";
        if (Check(hasMatchedStates, "SPH reference comparison") == false)
        {
            return false;
        }
    }

    settings.BoundaryEnabled = false;
    std::vector<FluidParticle> escaping(1u);
    escaping[0].Velocity.x = 1000.0f;
    if (Check(gpu.PrepareFixedStep(encoder, capabilities, escaping, settings, dt, source) == Result::Ready,
        "SPH escape prepare") == false)
    {
        return false;
    }
    Ref<RHIComputeTimestamp> timestamp;
    if (Check(gpu.DispatchFixedStep(timestamp), "SPH escape dispatch") == false)
    {
        return false;
    }
    const auto tickets = gpu.RequestStepReadback();
    std::vector<std::byte> state;
    std::vector<std::byte> dynamics;
    return Check(ReadSPHBuffer(tickets.m_Particles, state) && ReadSPHBuffer(tickets.m_Dynamics, dynamics)
        && GPUSPH::DecodeStep(state, dynamics, escaping) == false, "SPH grid escape rejected");
}

bool VerifySPHAdaptiveFrame(const Ref<RHIComputeCommandEncoder>& encoder,
    const RHIComputeCapabilities& capabilities, const std::string& source)
{
    using Result = GPUSPH::PrepareResult;
    const auto verify = [&](const char* name, const SPHSettings& settings,
        const std::vector<FluidParticle>& initial, float frameDt, uint32_t expectedSteps, bool expectsLimit,
        bool compareFixedCost = false)
    {
        SPHSolver cpu(settings);
        auto expected = initial;
        cpu.Step(expected, frameDt);
        if (Check(cpu.GetLastSubstepCount() == expectedSteps && cpu.WasLastSubstepLimitReached() == expectsLimit,
            "SPH adaptive CPU fixture") == false)
        {
            std::cerr << name << " cpu substeps=" << cpu.GetLastSubstepCount() << '\n';
            return false;
        }
        GPUSPH gpu;
        if (Check(gpu.PrepareAdaptiveFrame(encoder, capabilities, initial, settings, frameDt, source) == Result::Ready,
            "SPH adaptive prepare") == false)
        {
            return false;
        }
        const auto before = encoder->GetStatistics();
        Ref<RHIComputeTimestamp> timestamp;
        if (Check(gpu.DispatchAdaptiveFrame(timestamp), "SPH adaptive dispatch") == false)
        {
            return false;
        }
        const auto after = encoder->GetStatistics();
        if (Check(after.Readbacks == before.Readbacks && after.Buffers == before.Buffers
            && after.Dispatches == before.Dispatches + 1u + 7u * settings.MaximumSubsteps,
            "SPH adaptive GPU residence") == false)
        {
            return false;
        }

        const auto tickets = gpu.RequestStepReadback();
        const auto statisticsTicket = gpu.RequestAdaptiveStatistics();
        std::vector<std::byte> stateBytes;
        std::vector<std::byte> dynamicsBytes;
        std::vector<std::byte> statisticsBytes;
        std::vector<FluidParticle> actual(initial.size());
        GPUSPH::AdaptiveStatistics statistics;
        if (Check(ReadSPHBuffer(tickets.m_Particles, stateBytes) && ReadSPHBuffer(tickets.m_Dynamics, dynamicsBytes)
            && ReadSPHBuffer(statisticsTicket, statisticsBytes)
            && GPUSPH::DecodeStep(stateBytes, dynamicsBytes, actual)
            && GPUSPH::DecodeAdaptiveStatistics(statisticsBytes, statistics)
            && encoder->GetStatistics().Readbacks == before.Readbacks + 3u,
            "SPH adaptive final snapshots") == false)
        {
            return false;
        }
        if (Check(statistics.m_Substeps == expectedSteps && statistics.m_HasReachedLimit == expectsLimit
            && std::abs(statistics.m_MinimumDt - cpu.GetLastMinimumSubstepDeltaTime()) <= 1.0e-6f,
            "SPH adaptive substep statistics") == false)
        {
            std::cerr << name << " gpu substeps=" << statistics.m_Substeps
                << " gpu_min_dt=" << statistics.m_MinimumDt << " cpu_min_dt="
                << cpu.GetLastMinimumSubstepDeltaTime() << '\n';
            return false;
        }
        const auto close = [](float value, float reference, float scale, float tolerance)
        {
            return std::isfinite(value) && std::isfinite(reference)
                && std::abs(value - reference) <= tolerance * std::max(scale, std::abs(reference));
        };
        const auto vectorClose = [&](const Vec3& value, const Vec3& reference, float tolerance)
        {
            return close(value.x, reference.x, 1.0f, tolerance) && close(value.y, reference.y, 1.0f, tolerance)
                && close(value.z, reference.z, 1.0f, tolerance);
        };
        for (std::size_t index = 0u; index < actual.size(); ++index)
        {
            const auto& value = actual[index];
            const auto& reference = expected[index];
            if (Check(value.Mass == reference.Mass
                && close(value.Density, reference.Density, 1.0f, 2.0e-4f)
                && close(value.Pressure, reference.Pressure, std::max(1.0f, settings.RestDensity * settings.PressureStiffness), 2.0e-6f)
                && vectorClose(value.Force, reference.Force, 2.0e-3f)
                && vectorClose(value.Position, reference.Position, 5.0e-4f)
                && vectorClose(value.Velocity, reference.Velocity, 5.0e-4f), name) == false)
            {
                std::cerr << "particle=" << index << '\n';
                return false;
            }
        }

        std::cout << "adaptive=" << name << ", particles=" << initial.size() << ", substeps=" << statistics.m_Substeps
            << ", min_dt=" << statistics.m_MinimumDt << ", limit=" << statistics.m_HasReachedLimit
            << ", intermediate_readbacks=0";
        if (compareFixedCost == true)
        {
            GPUSPH fixed;
            if (Check(fixed.PrepareFixedStep(encoder, capabilities, initial, settings,
                frameDt / expectedSteps, source) == Result::Ready, "SPH fixed cost prepare") == false)
            {
                return false;
            }
            std::vector<Ref<RHIComputeTimestamp>> fixedTimestamps(expectedSteps);
            for (auto& fixedTimestamp : fixedTimestamps)
            {
                if (fixed.DispatchFixedStep(fixedTimestamp) == false)
                {
                    return false;
                }
            }
            glFinish();
            double adaptiveNs = 0.0;
            double fixedNs = 0.0;
            bool hasTiming = timestamp != nullptr && timestamp->Poll(adaptiveNs) == RHIComputeReadbackStatus::Ready;
            for (const auto& fixedTimestamp : fixedTimestamps)
            {
                double value = 0.0;
                hasTiming = hasTiming && fixedTimestamp != nullptr
                    && fixedTimestamp->Poll(value) == RHIComputeReadbackStatus::Ready;
                fixedNs += value;
            }
            if (hasTiming == true)
            {
                std::cout << ", adaptive_gpu_ms=" << adaptiveNs / 1.0e6 << ", fixed_gpu_ms=" << fixedNs / 1.0e6;
            }
            else
            {
                std::cout << ", gpu_timing=unavailable";
            }
        }
        std::cout << '\n';
        return true;
    };

    SPHSettings settings;
    settings.PressureStiffness = 0.0f;
    settings.Gravity = {};
    settings.MinimumTimeStep = 0.0f;
    settings.AccelerationTimeStepFactor = 0.0f;
    settings.BoundaryEnabled = true;
    settings.BoundaryRestitution = 0.5f;
    std::vector<FluidParticle> single(1u);
    single[0].Velocity.x = 4.0f;
    if (verify("velocity", settings, single, 0.025f, 3u, false) == false)
    {
        return false;
    }
    single[0].Position.x = 0.97f;
    if (verify("reflection", settings, single, 0.025f, 2u, false) == false)
    {
        return false;
    }
    single[0] = {};
    single[0].Force.x = 16.0f;
    settings.CFLFactor = 0.0f;
    settings.AccelerationTimeStepFactor = 0.25f;
    if (verify("acceleration", settings, single, 0.04f, 2u, false) == false)
    {
        return false;
    }
    settings.MinimumTimeStep = 0.03f;
    if (verify("minimum_dt", settings, single, 0.04f, 2u, false) == false)
    {
        return false;
    }
    settings.MinimumTimeStep = 0.0f;
    settings.MaximumSubsteps = 1u;
    if (verify("substep_limit", settings, single, 0.04f, 1u, true) == false)
    {
        return false;
    }
    single[0].Density = 3.0f;
    single[0].Pressure = 4.0f;
    if (verify("below_epsilon", settings, single, 0.5e-6f, 0u, false) == false)
    {
        return false;
    }

    settings = {};
    settings.BoundaryEnabled = true;
    settings.PressureStiffness = 100.0f;
    std::vector<FluidParticle> uniform(512u);
    for (std::size_t index = 0u; index < uniform.size(); ++index)
    {
        uniform[index].Position = {static_cast<float>(index % 8u) * 0.065f,
            static_cast<float>((index / 8u) % 8u) * 0.065f, static_cast<float>(index / 64u) * 0.065f};
        uniform[index].Mass = 0.25f;
    }
    return verify("uniform", settings, uniform, 0.01f, 3u, false, true);
}

bool VerifySPHDensity()
{
    OpenGLRHIDevice device;
    const auto encoder = device.CreateComputeCommandEncoder();
    const auto capabilities = device.QueryComputeCapabilities();
    const auto source = ReadShader("GPUSPH.comp");
    SPHSettings settings;
    SPHSolver cpu(settings);
    GPUSPH gpu;
    std::cout << "OpenGL: " << glGetString(GL_RENDERER) << '\n';

    using Result = GPUSPH::PrepareResult;
    const auto statistics = encoder == nullptr ? RHIComputeStatistics{} : encoder->GetStatistics();
    Ref<RHIComputeTimestamp> unusedTimestamp;
    if (Check(gpu.Prepare(encoder, capabilities, {}, settings.SmoothingRadius, source) == Result::Ready
        && gpu.DispatchDensity(unusedTimestamp) && !gpu.RequestDensityReadback()
        && encoder->GetStatistics().Dispatches == statistics.Dispatches, "SPH empty input") == false)
    {
        return false;
    }

    const auto compare = [&](const char* name, std::vector<FluidParticle> particles)
    {
        if (Check(gpu.Prepare(encoder, capabilities, particles, settings.SmoothingRadius, source) == Result::Ready,
            name) == false)
        {
            return false;
        }
        constexpr std::size_t iterations = 5u;
        Ref<RHIComputeTimestamp> warmup;
        if (gpu.DispatchDensity(warmup) == false)
        {
            return false;
        }
        std::array<Ref<RHIComputeTimestamp>, iterations> timestamps;
        const auto prior = encoder->GetStatistics();
        const auto gpuStart = std::chrono::steady_clock::now();
        for (auto& timestamp : timestamps)
        {
            if (gpu.DispatchDensity(timestamp) == false)
            {
                return false;
            }
        }
        // Timestamp取得が未対応でも検証できるよう、読み戻しを含まない同期経過時間を保持します。
        glFinish();
        const double gpuWallMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - gpuStart).count() / iterations;
        const auto ticket = gpu.RequestDensityReadback();
        std::vector<std::byte> bytes;
        if (ticket == nullptr)
        {
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        auto status = ticket->Poll(bytes);
        while (status == RHIComputeReadbackStatus::Pending && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            status = ticket->Poll(bytes);
        }
        std::vector<float> densities(particles.size());
        if (Check(status == RHIComputeReadbackStatus::Ready && GPUSPH::DecodeDensity(bytes, densities)
            && encoder->GetStatistics().Readbacks == prior.Readbacks + 1u, name) == false)
        {
            return false;
        }

        cpu.ComputeDensity(particles);
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t iteration = 0u; iteration < iterations; ++iteration)
        {
            cpu.ComputeDensity(particles);
        }
        const double cpuMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count() / iterations;

        double gpuNs = 0.0;
        bool hasTimestamps = true;
        const auto timestampDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        for (const auto& timestamp : timestamps)
        {
            double value = 0.0;
            if (timestamp == nullptr)
            {
                hasTimestamps = false;
                break;
            }
            auto timestampStatus = timestamp->Poll(value);
            while (timestampStatus == RHIComputeReadbackStatus::Pending
                && std::chrono::steady_clock::now() < timestampDeadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                timestampStatus = timestamp->Poll(value);
            }
            if (timestampStatus != RHIComputeReadbackStatus::Ready)
            {
                hasTimestamps = false;
                break;
            }
            gpuNs += value;
        }
        const double gpuMs = hasTimestamps ? gpuNs / (1.0e6 * iterations) : gpuWallMs;
        float maxAbsoluteError = 0.0f;
        float maxRelativeError = 0.0f;
        for (std::size_t index = 0u; index < particles.size(); ++index)
        {
            const float expected = particles[index].Density;
            const float error = std::abs(densities[index] - expected);
            const float relativeError = error / std::max(1.0f, std::abs(expected));
            if (Check(std::isfinite(densities[index]) && relativeError <= 2.0e-4f, name) == false)
            {
                std::cerr << "particle=" << index << " expected=" << expected << " actual=" << densities[index] << '\n';
                return false;
            }
            maxAbsoluteError = std::max(maxAbsoluteError, error);
            maxRelativeError = std::max(maxRelativeError, relativeError);
        }
        std::cout << name << ", particles=" << particles.size() << ", cpu_ms=" << cpuMs
            << ", gpu_ms=" << gpuMs << ", gpu_timer=" << (hasTimestamps ? "timestamp" : "wall")
            << ", max_abs_error=" << maxAbsoluteError << ", max_rel_error=" << maxRelativeError << '\n';
        return true;
    };

    std::vector<FluidParticle> boundary(5u);
    boundary[0].Position.x = -0.2f;
    boundary[1].Position.x = std::nextafter(-0.2f, 0.0f);
    boundary[2].Position.x = -0.1f;
    boundary[3].Position.x = 0.0f;
    boundary[4].Position.x = 0.1f;
    boundary[4].Mass = -1.0f;
    std::vector<FluidParticle> dense(513u);
    for (std::size_t index = 0u; index < dense.size(); ++index)
    {
        dense[index].Position = {static_cast<float>(index % 9u) * 0.003f,
            static_cast<float>((index / 9u) % 9u) * 0.003f, static_cast<float>(index / 81u) * 0.003f};
        dense[index].Mass = 0.001f * static_cast<float>(1u + index % 5u);
    }
    std::vector<FluidParticle> uniform(5120u);
    for (std::size_t index = 0u; index < uniform.size(); ++index)
    {
        uniform[index].Position = {static_cast<float>(index % 18u) * 0.065f,
            static_cast<float>((index / 18u) % 18u) * 0.065f, static_cast<float>(index / 324u) * 0.065f};
        uniform[index].Mass = 0.25f;
    }
    if (compare("single", std::vector<FluidParticle>(1u)) == false || compare("boundary", boundary) == false
        || compare("dense", dense) == false || compare("uniform", uniform) == false)
    {
        return false;
    }

    std::vector<FluidParticle> overCapacity(2u);
    overCapacity[1].Position = {100.0f, 100.0f, 100.0f};
    if (Check(gpu.Prepare(encoder, capabilities, overCapacity, settings.SmoothingRadius, source)
        == Result::CapacityExceeded && gpu.DispatchDensity(unusedTimestamp) == false
        && !gpu.RequestDensityReadback(), "SPH capacity detection") == false)
    {
        return false;
    }
    cpu.ComputeDensity(overCapacity);
    return Check(std::isfinite(overCapacity[0].Density) && overCapacity[0].Density > 0.0f
        && overCapacity[0].Density == overCapacity[1].Density, "SPH CPU fallback")
        && VerifySPHFixedSteps(encoder, capabilities, source)
        && VerifySPHAdaptiveFrame(encoder, capabilities, source);
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

int RunGPUSPHRenderDemo(bool verify)
{
    if (Check(glfwInit() == GLFW_TRUE, "GLFW initialization") == false)
    {
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, verify ? GLFW_FALSE : GLFW_TRUE);
    GLFWwindow* pWindow = glfwCreateWindow(640, 640, "Raven GPU SPH", nullptr, nullptr);
    bool successful = false;
    if (Check(pWindow != nullptr, "OpenGL 4.3 context"))
    {
        glfwMakeContextCurrent(pWindow);
        successful = Check(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) != 0,
            "OpenGL loader") && RunSPHResidentRender(*pWindow, verify);
        // 描画・Computeリソースは同じContextがcurrentのまま破棄されています。
        glfwDestroyWindow(pWindow);
    }
    glfwTerminate();
    if (verify && successful)
    {
        std::cout << "[PASS] GPU SPH resident render\n";
    }
    return successful ? 0 : 1;
}

int RunGPUSPHVerification()
{
    if (Check(glfwInit() == GLFW_TRUE, "GLFW initialization") == false)
    {
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* pWindow = glfwCreateWindow(32, 32, "Raven GPU SPH verification", nullptr, nullptr);
    bool successful = false;
    if (Check(pWindow != nullptr, "OpenGL 4.3 context"))
    {
        glfwMakeContextCurrent(pWindow);
        successful = Check(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) != 0,
            "OpenGL loader") && VerifySPHDensity();
        glfwDestroyWindow(pWindow);
    }
    glfwTerminate();
    if (successful == true)
    {
        std::cout << "[PASS] GPU SPH Density / fixed step / adaptive CFL verification\n";
    }
    return successful ? 0 : 1;
}

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
