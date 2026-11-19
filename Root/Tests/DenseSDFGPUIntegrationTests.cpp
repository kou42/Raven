#include "Raven/Physics/Field/DenseSDF.h"
#include "Raven/Platform/DirectX12/DX12SceneRHIDevice.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHIDevice.h"
#include "Raven/Platform/Vulkan/VulkanSceneRHIDevice.h"
#include "Raven/Renderer/RHI/RHIShaderBinaryLoader.h"
#include "Raven/Core/Window.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <thread>

namespace
{
using namespace Raven;
using namespace Raven::ph;
using math::Vec3;

constexpr auto ShaderDirectory = "Raven/Assets/Shaders/";
constexpr auto OutputDirectory = "../Tests/x64/ComputeShaders/";
constexpr float DistanceTolerance = 1.0e-4f;

bool Check(bool passed, const char* label)
{
    if (passed == false)
    {
        std::cerr << "[FAIL] Dense SDF: " << label << '\n';
    }
    return passed;
}

std::string ReadText(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool LoadShader(RHIBackend backend, bool sparse, RHIComputePipelineSpecification& shader)
{
    const std::string name = sparse ? "SparseSDF" : "DenseSDF";
    if (backend == RHIBackend::OpenGL)
    {
        shader.OpenGLSource = ReadText(std::string(ShaderDirectory) + name + ".comp");
        for (const auto* file : {"DenseSDF.inc", "SparseSDF.inc"})
        {
            if (sparse == false && std::string_view(file) == "SparseSDF.inc")
            {
                continue;
            }
            const std::string include = std::string("#include \"") + file + "\"";
            const auto marker = shader.OpenGLSource.find(include);
            const auto common = ReadText(std::string(ShaderDirectory) + file);
            if (marker == std::string::npos || common.empty())
            {
                return false;
            }
            shader.OpenGLSource.replace(marker, include.size(), common);
        }
        return true;
    }
    const bool dx12 = backend == RHIBackend::DirectX12;
    RHIShaderBinary binary;
    if (RHIShaderBinaryLoader::LoadForBackend(std::string(OutputDirectory) +
        name + (dx12 ? ".dxil" : ".spv"), backend, binary, dx12 ? "CSMain" : "main") == false)
    {
        return false;
    }
    if (dx12)
    {
        shader.DirectX12Bytecode.resize(binary.Code.size());
        std::memcpy(shader.DirectX12Bytecode.data(), binary.Code.data(), binary.Code.size());
    }
    else
    {
        shader.VulkanSpirv.resize(binary.Code.size() / sizeof(std::uint32_t));
        std::memcpy(shader.VulkanSpirv.data(), binary.Code.data(), binary.Code.size());
    }
    return true;
}

struct TestCase
{
    const char* Name;
    DenseSDFSource Source;
    DenseSDFGrid Grid;
    std::function<float(Vec3)> Reference;
    bool Signed;
};

Vec3 RotateZ(Vec3 p, float angle)
{
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
}

TestCase MakeBox(bool thin)
{
    constexpr DenseSDFSource::Triangle triangles[] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7},
        {0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
    const Vec3 half{0.65f, 0.45f, thin ? 0.065f : 0.38f};
    const Vec3 center{0.08f, -0.09f, 0.03f};
    constexpr float angle = 0.41f;
    Vec3 vertices[] = {{-half.x, -half.y, -half.z}, {half.x, -half.y, -half.z},
        {half.x, half.y, -half.z}, {-half.x, half.y, -half.z}, {-half.x, -half.y, half.z},
        {half.x, -half.y, half.z}, {half.x, half.y, half.z}, {-half.x, half.y, half.z}};
    for (auto& vertex : vertices)
    {
        vertex = RotateZ(vertex, angle) + center;
    }
    auto source = DenseSDFSource::CreateMesh(vertices, triangles, DenseSDFSource::Mode::ClosedMesh);
    return {thin ? "thin-box" : "transformed-box", source.value(),
        {{-1.0f, -1.0f, -0.7f}, {0.08f, 0.09f, thin ? 0.03f : 0.07f},
            {25, 23, thin ? 47u : 21u}},
        [=](Vec3 p)
        {
            const auto local = RotateZ(p - center, -angle);
            const Vec3 q{std::abs(local.x) - half.x, std::abs(local.y) - half.y, std::abs(local.z) - half.z};
            return Vec3(std::max(q.x, 0.0f), std::max(q.y, 0.0f), std::max(q.z, 0.0f)).Length() +
                std::min(std::max({q.x, q.y, q.z}), 0.0f);
        }, true};
}

bool MakeOpenCases(std::vector<TestCase>& cases)
{
    const Vec3 plane[] = {{-0.6f, -0.4f, 0}, {0.6f, -0.4f, 0}, {0.6f, 0.4f, 0}, {-0.6f, 0.4f, 0}};
    const DenseSDFSource::Triangle sides[] = {{0, 1, 2}, {0, 2, 3}};
    const DenseSDFSource::Triangle degenerate[] = {{0, 1, 1}};
    if (DenseSDFSource::CreateMesh(plane, sides, DenseSDFSource::Mode::ClosedMesh).has_value() ||
        DenseSDFSource::CreateMesh(plane, degenerate, DenseSDFSource::Mode::ClosedMesh).has_value())
    {
        return false;
    }
    const DenseSDFGrid grid{{-0.95f, -0.85f, -0.55f}, {0.10f, 0.10f, 0.10f}, {19, 17, 11}};
    cases.push_back({"open-plane", DenseSDFSource::CreateMesh(plane, sides, DenseSDFSource::Mode::UnsignedMesh).value(),
        grid, [](Vec3 p)
        {
            return Vec3(std::max(std::abs(p.x) - 0.6f, 0.0f),
                std::max(std::abs(p.y) - 0.4f, 0.0f), p.z).Length();
        }, false});
    cases.push_back({"degenerate-segment", DenseSDFSource::CreateMesh(plane, degenerate, DenseSDFSource::Mode::UnsignedMesh).value(),
        grid, [](Vec3 p) { return Vec3(p.x - std::clamp(p.x, -0.6f, 0.6f), p.y + 0.4f, p.z).Length(); }, false});
    return true;
}

bool WriteSlice(const char* backend, const TestCase& test, std::span<const float> distances)
{
    const auto& size = test.Grid.Size;
    const auto z = size[2] / 2;
    std::ofstream out(std::string(OutputDirectory) + "DenseSDF-" + backend + "-" + test.Name + ".svg");
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << size[0] * 16 << "\" height=\""
        << size[1] * 16 << "\" viewBox=\"0 0 " << size[0] << ' ' << size[1] << "\">\n";
    const float band = 0.5f * std::min(test.Grid.Spacing.x, test.Grid.Spacing.y);
    for (std::uint32_t y = 0; y < size[1]; ++y)
    {
        for (std::uint32_t x = 0; x < size[0]; ++x)
        {
            const float distance = distances[x + size[0] * (y + size[1] * z)];
            const int intensity = static_cast<int>(170.0f * std::clamp(std::abs(distance) / 0.5f, 0.0f, 1.0f));
            const int r = std::abs(distance) <= band ? 245 : (distance < 0.0f ? 35 : 244 - intensity / 2);
            const int b = std::abs(distance) <= band ? 245 : (distance < 0.0f ? 244 - intensity / 2 : 35);
            out << "<rect x=\"" << x << "\" y=\"" << size[1] - 1 - y
                << "\" width=\"1\" height=\"1\" fill=\"rgb(" << r << ","
                << (std::abs(distance) <= band ? 245 : 94) << "," << b << ")\"/>\n";
        }
    }
    out << "</svg>\n";
    return out.good();
}

bool Compare(const char* backend, const TestCase& test, std::span<const float> actual)
{
    const auto& size = test.Grid.Size;
    std::vector<float> reference;
    reference.reserve(actual.size());
    float maximum = 0.0f;
    float cpuMaximum = 0.0f;
    float cpuGpuMaximum = 0.0f;
    std::size_t negative = 0;
    for (std::uint32_t z = 0; z < size[2]; ++z)
    {
        for (std::uint32_t y = 0; y < size[1]; ++y)
        {
            for (std::uint32_t x = 0; x < size[0]; ++x)
            {
                const auto position = test.Grid.GetCellCenter(x, y, z);
                const float expected = test.Reference(position);
                const float cpu = test.Source.Evaluate(position);
                const float gpu = actual[reference.size()];
                if (std::isfinite(gpu) == false || std::isfinite(cpu) == false ||
                    (test.Signed == false && gpu < 0.0f))
                {
                    return Check(false, "finite and signed/unsigned samples");
                }
                maximum = std::max(maximum, std::abs(gpu - expected));
                cpuMaximum = std::max(cpuMaximum, std::abs(cpu - expected));
                cpuGpuMaximum = std::max(cpuGpuMaximum, std::abs(gpu - cpu));
                negative += gpu < -DistanceTolerance ? 1 : 0;
                reference.push_back(expected);
            }
        }
    }
    float minimumDot = 1.0f;
    float minimumAnalyticDot = 1.0f;
    std::size_t normals = 0;
    for (std::uint32_t z = 1; z + 1 < size[2]; ++z)
    {
        for (std::uint32_t y = 1; y + 1 < size[1]; ++y)
        {
            for (std::uint32_t x = 1; x + 1 < size[0]; ++x)
            {
                const std::size_t index = x + size[0] * (y + size[1] * z);
                if (std::abs(reference[index]) > 0.12f)
                {
                    continue;
                }
                // 極値・メッシュ稜線などで差分の向きが定まらない点を避けます。
                const Vec3 difference{
                    reference[index + 1] - reference[index - 1],
                    reference[index + size[0]] - reference[index - size[0]],
                    reference[index + size[0] * size[1]] - reference[index - size[0] * size[1]]};
                const auto spacing = test.Grid.Spacing;
                if (Vec3(difference.x / spacing.x, difference.y / spacing.y,
                    difference.z / spacing.z).Length() < 1.0f)
                {
                    continue;
                }
                Vec3 cpuNormal{}, gpuNormal{};
                if (test.Grid.TryGetNormal(reference, {x, y, z}, cpuNormal) == false ||
                    test.Grid.TryGetNormal(actual, {x, y, z}, gpuNormal) == false)
                {
                    return Check(false, "valid boundary normal");
                }
                minimumDot = std::min(minimumDot, Vec3::Dot(cpuNormal, gpuNormal));
                if (test.Source.GetMode() == DenseSDFSource::Mode::Sphere)
                {
                    const auto radial = (test.Grid.GetCellCenter(x, y, z) - test.Source.GetCenter()).Normalized();
                    minimumAnalyticDot = std::min(minimumAnalyticDot, Vec3::Dot(radial, gpuNormal));
                }
                ++normals;
            }
        }
    }
    Vec3 unused{};
    std::cout << backend << " " << test.Name << " voxels=" << actual.size()
        << " cpuMax=" << cpuMaximum << " gpuMax=" << maximum << " cpuGpuMax=" << cpuGpuMaximum
        << " normals=" << normals
        << " minNormalDot=" << minimumDot << " minAnalyticDot=" << minimumAnalyticDot << '\n';
    return Check(cpuMaximum <= DistanceTolerance && maximum <= DistanceTolerance &&
        cpuGpuMaximum <= DistanceTolerance, "distance tolerance (1e-4 m)") &&
        Check(test.Signed == (negative > 0), "interior or unsigned contract") &&
        Check(normals > 0 && minimumDot >= 0.999f && minimumAnalyticDot >= 0.998f, "normal tolerance") &&
        Check(test.Grid.TryGetNormal(actual, {0, 0, 0}, unused) == false, "grid boundary has no central normal") &&
        Check(WriteSlice(backend, test, actual), "write GPU distance slice");
}

bool RunCases(RHIDevice& device, const char* backend, RHISceneFrameLifecycle* frame)
{
    RHIComputePipelineSpecification shader;
    auto encoder = device.CreateComputeCommandEncoder();
    if (Check(encoder != nullptr && LoadShader(device.GetBackend(), false, shader), "Compute encoder and shader") == false)
    {
        return false;
    }
    std::vector<TestCase> cases;
    const Vec3 center{0.03f, -0.02f, 0.04f};
    constexpr float radius = 0.61f;
    const auto sphere = DenseSDFSource::CreateSphere(center, radius).value();
    const auto sphereDistance = [=](Vec3 p) { return (p - center).Length() - radius; };
    cases.push_back({"sphere-coarse", sphere, {{-1.0f, -1.0f, -1.0f}, {0.13f, 0.11f, 0.12f}, {17, 19, 17}}, sphereDistance, true});
    cases.push_back({"sphere-fine", sphere, {{-1.0f, -1.0f, -1.0f}, {0.058f, 0.064f, 0.062f}, {35, 33, 33}}, sphereDistance, true});
    cases.push_back(MakeBox(false));
    cases.push_back(MakeBox(true));
    if (Check(MakeOpenCases(cases), "reject open/degenerate signed input") == false)
    {
        return false;
    }
    for (const auto& test : cases)
    {
        auto generator = CreateRef<DenseSDFGenerator>();
        if (Check(generator->Prepare(*encoder, shader, test.Source, test.Grid), "prepare Dense SDF") == false)
        {
            return false;
        }
        auto result = CreateRef<Ref<RHIComputeReadback>>();
        if (frame == nullptr)
        {
            *result = generator->Record(*encoder);
        }
        else if (encoder->QueueFrame([generator, result](auto& commands)
            {
                *result = generator->Record(commands);
                return *result != nullptr;
            }) == false || frame->BeginFrame() != RHIFrameResult::Success ||
            frame->EndFrame() != RHIFrameResult::Success || frame->Present() == RHIFrameResult::FatalError ||
            frame->Resize(320, 240) == false)
        {
            return Check(false, "submit and retire Explicit Frame");
        }
        if (Check(*result != nullptr && generator->GetTexture() != nullptr, "3D texture and readback ticket") == false)
        {
            return false;
        }
        std::vector<std::byte> bytes;
        auto status = (*result)->Poll(bytes);
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (status == RHIComputeReadbackStatus::Pending && std::chrono::steady_clock::now() < limit)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            status = (*result)->Poll(bytes);
        }
        const auto count = test.Grid.GetVoxelCount();
        if (Check(status == RHIComputeReadbackStatus::Ready && bytes.size() == count * sizeof(float), "sample readback") == false)
        {
            return false;
        }
        std::vector<float> distances(count);
        std::memcpy(distances.data(), bytes.data(), bytes.size());
        if (Compare(backend, test, distances) == false)
        {
            return false;
        }
    }
    return true;
}
} // namespace

bool LoadSDFTestShader(Raven::RHIBackend backend, bool sparse, Raven::RHIComputePipelineSpecification& shader)
{
    return LoadShader(backend, sparse, shader);
}

bool RunSparseSDFCases(Raven::RHIDevice& device, const char* backend, Raven::RHISceneFrameLifecycle* frame);

bool RunDenseSDFGPUIntegrationTests(Raven::RHIBackend backend, bool sparse)
{
    using namespace Raven;
    const auto run = sparse ? RunSparseSDFCases : RunCases;
    if (backend == RHIBackend::DirectX12 || backend == RHIBackend::Vulkan)
    {
        auto window = Window::Create(WindowSpecification("Dense SDF", 320, 240, backend));
        if (window == nullptr)
        {
            return false;
        }
        if (backend == RHIBackend::DirectX12)
        {
            DX12SceneContext context;
            DX12SceneRHIDevice device(context);
            return Check(context.Init(*window), "DX12 context") && run(device, "dx12", &context);
        }
        VulkanSceneContext context;
        VulkanSceneRHIDevice device(context);
        return Check(context.Init(*window), "Vulkan context") && run(device, "vulkan", &context);
    }
    if (glfwInit() != GLFW_TRUE)
    {
        return Check(false, "GLFW initialization");
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Dense SDF", nullptr, nullptr);
    bool passed = false;
    if (window != nullptr)
    {
        glfwMakeContextCurrent(window);
        OpenGLRHIDevice device;
        passed = gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) != 0 &&
            run(device, "opengl", nullptr);
        glfwDestroyWindow(window);
    }
    glfwTerminate();
    return Check(passed, "OpenGL Dense SDF");
}
