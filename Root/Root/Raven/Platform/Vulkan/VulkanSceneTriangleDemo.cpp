#include "VulkanSceneTriangleDemo.h"
#include "VulkanSceneRHIDevice.h"

#include "Raven/Core/Window.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>
#include <limits>
#include <utility>

namespace Raven
{
namespace
{
// RavenのMat4はrow-major、GLSLのmat4はcolumn-majorです。
// Push Constantへ送る直前に転置配置し、座標変換の向きを保ちます。
std::array<float, 16> ToColumnMajor(const math::Mat4& matrix)
{
    std::array<float, 16> result{};
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            result[column * 4 + row] = matrix.m[row][column];
        }
    }
    return result;
}

math::Mat4 FromColumnMajor(const std::array<float, 16>& values)
{
    math::Mat4 result{};
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            result.m[row][column] = values[column * 4 + row];
        }
    }
    return result;
}
} // namespace
bool VulkanSceneTriangleDemo::Init(Window& window,
    const RHIShaderBinary& vertexShader, const RHIShaderBinary& fragmentShader)
{
    Shutdown();
    if (window.GetBackend() != RHIBackend::Vulkan ||
        vertexShader.Format != RHIShaderBinaryFormat::SPIRV ||
        fragmentShader.Format != RHIShaderBinaryFormat::SPIRV)
    {
        return false;
    }

    m_Window = &window;
    m_VertexShader = vertexShader;
    m_FragmentShader = fragmentShader;
    if (m_Context.Init(window) == false)
    {
        std::cerr << "Vulkan Scene Triangle: Scene Context initialization failed.\n";
        Shutdown();
        return false;
    }

    // 既存SceneCameraを利用し、右手系で-Z方向を見るViewを設定します。
    // RavenのPerspectiveはOpenGLのNDC深度を返すため、Draw時にVulkanへ補正します。
    m_Camera = SceneCamera();
    const VkExtent2D extent = m_Context.GetExtent();
    m_Camera.SetViewportSize(static_cast<float>(extent.width),
        static_cast<float>(extent.height));
    m_Camera.SetViewMatrix(math::Mat4::LookAt(
        {0.0f, 0.0f, 2.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}));

    // 手前のMeshを先に描画し、奥のMeshがDepth Testで隠れることを検証します。
    const std::vector<Vertex> left = {
        {{-0.3f, -0.6f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.3f, -0.6f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.0f,  0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}}
    };
    const std::vector<Vertex> right = {
        {{-0.3f, -0.6f, 0.0f}, {0.0f, 1.0f, 1.0f}},
        {{ 0.3f, -0.6f, 0.0f}, {1.0f, 0.0f, 1.0f}},
        {{ 0.0f,  0.6f, 0.0f}, {1.0f, 1.0f, 0.0f}}
    };
    const std::vector<uint32_t> indices = {0, 1, 2};
    if (AddMesh(right, indices) == false || AddMesh(left, indices) == false ||
        AddMesh(left, indices) == false)
    {
        std::cerr << "Vulkan Scene Triangle: Mesh creation failed.\n";
        Shutdown();
        return false;
    }
    // 同じローカル座標を独立したModel行列で左右へ配置します。
    auto rightModel = m_Meshes[0].Model;
    auto leftModel = m_Meshes[1].Model;
    auto farModel = m_Meshes[2].Model;
    leftModel[12] = -0.15f;
    rightModel[12] = 0.15f;
    rightModel[14] = 0.4f; // Cameraに近い右Meshが重複部分で前面に表示されます。
    farModel[12] = -0.05f;
    farModel[14] = -0.4f; // 追加Meshは奥に配置し、登録順に依存しないBlendを検証します。
    if (SetMeshTransform(0, rightModel) == false ||
        SetMeshTransform(1, leftModel) == false ||
        SetMeshTransform(2, farModel) == false)
    {
        Shutdown();
        return false;
    }
    // 透明Meshは手前を先に登録しても、描画時に奥からソートします。
    if (SetMeshMaterial(1, {1.0f, 0.8f, 0.8f, 0.65f}, true) == false ||
        SetMeshMaterial(2, {0.5f, 0.8f, 1.0f, 0.45f}, true) == false)
    {
        Shutdown();
        return false;
    }
    if (CreatePipeline() == false)
    {
        std::cerr << "Vulkan Scene Triangle: Graphics Pipeline creation failed.\n";
        Shutdown();
        return false;
    }
    return true;
}

bool VulkanSceneTriangleDemo::AddMesh(
    const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    if (m_Window == nullptr || m_Context.GetDevice().IsValid() == false ||
        m_Context.GetActiveCommandBuffer() != VK_NULL_HANDLE ||
        vertices.empty() == true || indices.empty() == true ||
        vertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(Vertex) ||
        indices.size() > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t))
    {
        return false;
    }
    for (const uint32_t index : indices)
    {
        if (index >= vertices.size())
        {
            return false;
        }
    }

    VulkanSceneRHIDevice device(m_Context);
    RHIBufferSpecification vertexSpecification{};
    vertexSpecification.Size = static_cast<uint32_t>(vertices.size() * sizeof(Vertex));
    vertexSpecification.Usage = RHIBufferUsage::Vertex;
    vertexSpecification.DebugName = "Vulkan Scene Mesh Vertex";

    RHIBufferSpecification indexSpecification{};
    indexSpecification.Size = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));
    indexSpecification.Usage = RHIBufferUsage::Index;
    indexSpecification.DebugName = "Vulkan Scene Mesh Index";

    // 両Bufferの生成が成功してからSceneに登録し、途中失敗で半端なMeshを残しません。
    Mesh mesh;
    mesh.VertexBuffer = device.CreateBuffer(vertexSpecification, vertices.data());
    if (mesh.VertexBuffer == nullptr)
    {
        return false;
    }
    mesh.IndexBuffer = device.CreateBuffer(indexSpecification, indices.data());
    if (mesh.IndexBuffer == nullptr)
    {
        return false;
    }
    mesh.IndexCount = static_cast<uint32_t>(indices.size());
    // ローカル中心を登録時に計算し、Camera移動時のソートは行列計算だけで済ませます。
    for (const Vertex& vertex : vertices)
    {
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            mesh.LocalCenter[axis] += vertex.Position[axis] /
                static_cast<float>(vertices.size());
        }
    }
    m_Meshes.push_back(std::move(mesh));
    return true;
}

bool VulkanSceneTriangleDemo::SetMeshTransform(
    std::size_t meshIndex, const std::array<float, 16>& model)
{
    if (meshIndex >= m_Meshes.size())
    {
        return false;
    }
    // DrawFrameは行列の値をPush Constantへコピーします。
    m_Meshes[meshIndex].Model = model;
    return true;
}

bool VulkanSceneTriangleDemo::SetMeshMaterial(
    std::size_t meshIndex, const std::array<float, 4>& tint, bool alphaBlend)
{
    if (meshIndex >= m_Meshes.size())
    {
        return false;
    }
    for (float component : tint)
    {
        if (std::isfinite(component) == false || component < 0.0f ||
            component > 1.0f)
        {
            return false;
        }
    }
    m_Meshes[meshIndex].Tint = tint;
    m_Meshes[meshIndex].AlphaBlend = alphaBlend;
    return true;
}

bool VulkanSceneTriangleDemo::ClearMeshes()
{
    if (m_Context.GetDevice().IsValid() == false ||
        m_Context.GetActiveCommandBuffer() != VK_NULL_HANDLE ||
        m_Context.GetDevice().WaitIdle() == false)
    {
        return false;
    }
    // 記録済みDrawのGPU参照が終わってからScene側のBuffer参照を解放します。
    m_Meshes.clear();
    return true;
}

bool VulkanSceneTriangleDemo::CreatePipeline()
{
    RHIGraphicsPipelineSpecification specification;
    specification.VertexShader = m_VertexShader;
    specification.FragmentShader = m_FragmentShader;
    specification.VertexBindings = {{ 0, sizeof(Vertex) }};
    specification.VertexAttributes = {
        { 0, 0, ShaderDataType::Float3, 0 },
        { 1, 0, ShaderDataType::Float3, sizeof(Vertex::Position) }
    };
    specification.Cull = CullMode::None;
    specification.DepthFormat = RHIDepthFormat::D32Float;
    specification.DepthTest = true;
    specification.DepthWrite = true;
    specification.DebugName = "Vulkan Scene Triangle";
    switch (m_Context.GetColorFormat())
    {
    case VK_FORMAT_R8G8B8A8_UNORM:
        specification.ColorFormat = RHIColorFormat::RGBA8Unorm;
        break;
    case VK_FORMAT_B8G8R8A8_UNORM:
        specification.ColorFormat = RHIColorFormat::BGRA8Unorm;
        break;
    case VK_FORMAT_R8G8B8A8_SRGB:
        specification.ColorFormat = RHIColorFormat::RGBA8Srgb;
        break;
    case VK_FORMAT_B8G8R8A8_SRGB:
        specification.ColorFormat = RHIColorFormat::BGRA8Srgb;
        break;
    default:
        // SwapChainの実FormatとPipelineのAttachment Formatを一致させます。
        std::cerr << "Vulkan Scene Triangle: Unsupported SwapChain color format: "
            << static_cast<int>(m_Context.GetColorFormat()) << '\n';
        return false;
    }
    m_Pipeline = m_Context.CreateGraphicsPipeline(specification);
    if (m_Pipeline == nullptr)
    {
        return false;
    }
    // BlendはDepth Writeを無効にした別Pipelineとし、Opaqueと混在可能にします。
    specification.Blend = true;
    specification.DepthWrite = false;
    specification.DebugName = "Vulkan Scene Triangle Transparent";
    m_TransparentPipeline = m_Context.CreateGraphicsPipeline(specification);
    return m_TransparentPipeline != nullptr;
}

RHIFrameResult VulkanSceneTriangleDemo::DrawFrame()
{
    if (m_Window == nullptr || m_Pipeline == nullptr ||
        m_TransparentPipeline == nullptr)
    {
        return RHIFrameResult::FatalError;
    }
    for (const Mesh& mesh : m_Meshes)
    {
        if (mesh.VertexBuffer == nullptr || mesh.IndexBuffer == nullptr)
        {
            return RHIFrameResult::FatalError;
        }
    }

    // View空間のZが小さい（Cameraから遠い）Meshから透明描画します。
    // Meshの頂点平均を代表点とする近似のため、交差する透明形状は完全には解決しません。
    std::vector<std::size_t> transparentIndices;
    transparentIndices.reserve(m_Meshes.size());
    for (std::size_t index = 0; index < m_Meshes.size(); ++index)
    {
        if (m_Meshes[index].AlphaBlend == true)
        {
            transparentIndices.push_back(index);
        }
    }
    const math::Mat4 view = m_Camera.GetViewMatrix();
    const auto viewDepth = [this, &view](std::size_t index)
    {
        const Mesh& mesh = m_Meshes[index];
        const math::Mat4 modelView = view * FromColumnMajor(mesh.Model);
        const auto& center = mesh.LocalCenter;
        return modelView.m[2][0] * center[0] +
            modelView.m[2][1] * center[1] +
            modelView.m[2][2] * center[2] + modelView.m[2][3];
    };
    std::stable_sort(transparentIndices.begin(), transparentIndices.end(),
        [&viewDepth](std::size_t left, std::size_t right)
        {
            return viewDepth(left) < viewDepth(right);
        });

    const RHIFrameResult begin = m_Context.BeginFrame();
    if (begin != RHIFrameResult::Success)
    {
        return begin;
    }

    VulkanSceneCommandList commands(m_Context);
    // RavenのPerspectiveのNDC z=[-1,1]をVulkanの[0,1]へ変換します。
    // 同時にYを反転し、Vulkanの正のViewport Heightと整合させます。
    const math::Mat4 vulkanClipCorrection(
        1.0f,  0.0f, 0.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f,
        0.0f,  0.0f, 0.5f, 0.5f,
        0.0f,  0.0f, 0.0f, 1.0f);
    const math::Mat4 viewProjection = vulkanClipCorrection *
        m_Camera.GetProjectionMatrix() * m_Camera.GetViewMatrix();
    // Opaqueの後、透明MeshをCameraから遠い順に描画します。
    // BlendのDepth Writeは無効のまま、OpaqueのDepthとは比較します。
    for (uint32_t pass = 0; pass < 2; ++pass)
    {
        const bool transparentPass = pass == 1;
        const auto& pipeline = transparentPass == true ?
            m_TransparentPipeline : m_Pipeline;
        if (commands.BindPipeline(pipeline) == false)
        {
            Shutdown();
            return RHIFrameResult::FatalError;
        }
        const std::size_t drawCount = transparentPass == true ?
            transparentIndices.size() : m_Meshes.size();
        for (std::size_t draw = 0; draw < drawCount; ++draw)
        {
            const Mesh& mesh = transparentPass == true ?
                m_Meshes[transparentIndices[draw]] : m_Meshes[draw];
            if (mesh.AlphaBlend != transparentPass)
            {
                continue;
            }
            // CameraとMeshのModelを合成して、DrawごとにPush Constantを更新します。
            const auto clipTransform = ToColumnMajor(
                viewProjection * FromColumnMajor(mesh.Model));
            if (commands.SetClipTransform(clipTransform) == false ||
                commands.SetMaterialTint(mesh.Tint) == false ||
                commands.DrawIndexed(mesh.VertexBuffer, mesh.IndexBuffer,
                    mesh.IndexCount) == false)
            {
                Shutdown();
                return RHIFrameResult::FatalError;
            }
        }
    }
    const RHIFrameResult end = m_Context.EndFrame();
    if (end != RHIFrameResult::Success)
    {
        Shutdown();
        return end;
    }
    const RHIFrameResult present = m_Context.Present();
    if (present == RHIFrameResult::FatalError)
    {
        Shutdown();
    }
    return present;
}

bool VulkanSceneTriangleDemo::Resize(uint32_t width, uint32_t height)
{
    if (m_Window == nullptr || width == 0 || height == 0 ||
        m_Context.Resize(width, height) == false)
    {
        return false;
    }
    // Resize時にScene Contextが旧RenderPass用Pipelineを無効化します。
    // 新SwapChainのFormatを読み直してから再生成します。
    m_Pipeline.reset();
    m_TransparentPipeline.reset();
    m_Camera.SetViewportSize(static_cast<float>(width), static_cast<float>(height));
    return CreatePipeline();
}

void VulkanSceneTriangleDemo::Shutdown()
{
    // BufferはGPUが参照しているため、必ずWaitIdle後、Device破棄前に解放します。
    if (m_Context.GetDevice().IsValid() == true)
    {
        m_Context.GetDevice().WaitIdle();
    }
    m_Pipeline.reset();
    m_TransparentPipeline.reset();
    // native VkBufferを所有するRefはContextのVkDeviceより先に破棄します。
    m_Meshes.clear();
    m_Context.Shutdown();
    m_VertexShader = {};
    m_FragmentShader = {};
    m_Window = nullptr;
}
} // namespace Raven
