// GPU Contextを作成してTexture upload診断を検証する独立した統合テストです。
// GLFW / GLAD / Raven本体をリンクし、描画可能なOpenGL環境で実行してください。
// 通常のUITextReflowTestsはGPU不要のまま維持します。
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHITexture.h"
#include "Raven/UI/Text/UIFontAtlasBuilder.h"
#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Docking/UIDockSpace.h"
#include "Raven/UI/Widgets/UIPanel.h"
#include "Raven/Core/Window.h"
#include "Raven/Platform/DirectX12/DX12SceneRuntime.h"
#include "Raven/Platform/Vulkan/VulkanSceneRuntime.h"
#include "Raven/Platform/Vulkan/VulkanSceneRHIDevice.h"
#include "Raven/Platform/Vulkan/VulkanSceneCommandList.h"
#include "Raven/Assets/RHIShaderAsset.h"
#include "Raven/Platform/DirectX12/DX12OffscreenRenderTarget.h"
#include "Raven/UI/Rendering/OpenGLUIRenderer.h"
#include "Raven/UI/Svg/UISvg.h"
#include <algorithm>
#include <vector>
#include <cstring>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <cmath>
#include <array>

namespace
{
bool Check(bool condition, const char* label)
{
    if (condition == false)
    {
        std::cerr << "[FAIL] " << label << '\n';
        return false;
    }
    return true;
}

bool TestUpload()
{
    Raven::TextureSpecification specification{};
    specification.Width = 2u;
    specification.Height = 2u;
    specification.Format = Raven::TextureFormat::RGBA8;
    specification.GenerateMips = false;
    const std::uint8_t pixels[16] =
    {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u, 255u, 255u, 255u, 255u
    };
    Raven::TextureCreationFailure failure = Raven::TextureCreationFailure::UploadFailed;
    Raven::Ref<Raven::Texture> texture =
        Raven::Texture::Create(specification, pixels, sizeof(pixels), &failure);
    if (Check(texture != nullptr, "valid diagnostic Texture creation") == false ||
        Check(failure == Raven::TextureCreationFailure::None, "valid upload diagnostic") == false)
    {
        return false;
    }

    // 単なるGL errorなしではなく、実際のGPU Texture内容まで読み戻して確認します。
    std::uint8_t readback[sizeof(pixels)]{};
    glBindTexture(GL_TEXTURE_2D, texture->GetID());
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    if (Check(glGetError() == GL_NO_ERROR, "Texture readback GL error") == false)
    {
        return false;
    }
    for (std::size_t index = 0u; index < sizeof(pixels); ++index)
    {
        if (Check(readback[index] == pixels[index], "uploaded pixels match readback") == false)
        {
            return false;
        }
    }

    Raven::OpenGLRHITexture rhiTexture(
        Raven::RHITextureSpecification{ 2u, 2u, Raven::RHITextureFormat::RGBA8,
            Raven::RHITextureUsage::Sampled, false, "UI upload test" });
    if (Check(rhiTexture.GetRendererID() != 0u, "RHI Texture created") == false ||
        Check(rhiTexture.TrySetData(pixels, sizeof(pixels) - 1u) == false,
            "RHI rejects incorrect upload size") == false ||
        Check(rhiTexture.TrySetData(nullptr, sizeof(pixels)) == false,
            "RHI rejects null upload") == false ||
        Check(rhiTexture.TrySetData(pixels, sizeof(pixels)) == true,
            "RHI accepts valid upload") == false)
    {
        return false;
    }

    // 既存GL errorをTexture転送の成功として扱わないことを確認します。
    while (glGetError() != GL_NO_ERROR)
    {
    }
    glBindTexture(GL_TEXTURE_2D, texture->GetID());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, -1);
    if (Check(glGetError() == GL_INVALID_ENUM, "GL error injection") == false)
    {
        return false;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, -1);
    if (Check(rhiTexture.TrySetData(pixels, sizeof(pixels)) == false,
        "RHI rejects preexisting GL error") == false)
    {
        return false;
    }
    while (glGetError() != GL_NO_ERROR)
    {
    }
    return true;
}

bool TestFontAtlas()
{
    std::string fontPath;
#if defined(_MSC_VER)
    char* configured = nullptr;
    size_t configuredLength = 0u;
    const errno_t environmentResult = _dupenv_s(
        &configured, &configuredLength, "RAVEN_UI_TEST_FONT");
    if (environmentResult == 0 && configured != nullptr && configured[0] != '\0')
    {
        fontPath = configured;
    }
    std::free(configured);
#else
    const char* configured = std::getenv("RAVEN_UI_TEST_FONT");
    if (configured != nullptr && configured[0] != '\0')
    {
        fontPath = configured;
    }
#endif
    if (fontPath.empty())
    {
        std::cout << "[SKIP] GPU Font Atlas: set RAVEN_UI_TEST_FONT to a TTF/TTC path\n";
        return true;
    }
    if (Check(std::filesystem::is_regular_file(fontPath), "configured GPU test font exists") == false)
    {
        return false;
    }
    Raven::UIFontAtlas atlas;
    Raven::UIFontAtlasBuildOptions options{};
    options.AtlasWidth = 128u;
    options.AtlasHeight = 128u;
    Raven::UIFontAtlasBuildFailure failure = Raven::UIFontAtlasBuildFailure::TextureUploadFailed;
    return Check(Raven::UIFontAtlasBuilder::BuildFromFile(
            fontPath, { 65u }, options, atlas, &failure), "GPU Font Atlas build") &&
        Check(failure == Raven::UIFontAtlasBuildFailure::None, "GPU Font Atlas diagnostic");
}

bool CheckRedFrame(GLFWwindow* window, Raven::UIContext& context, bool requireCacheHit)
{
    int width = 0;
    int height = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(window, &width, &height);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (Check(width > 0 && height > 0 && framebufferWidth > 0 && framebufferHeight > 0,
        "UI GPU test drawable dimensions") == false)
    {
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    // Layout変更後の通常生成→安定Frame採取→Replayを同じGPUで確認します。
    for (int frame = 0; frame < 3; ++frame)
    {
        context.BeginFrame({ static_cast<float>(width), static_cast<float>(height) },
            { static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight) }, 1.0f, 1.0f);
        context.EndFrame();
    }
    if (requireCacheHit == true && Check(context.GetLastFrameMetrics().DrawListCache.HitCount > 0u,
        "GPU cache replay actually used") == false)
    {
        return false;
    }
    std::uint8_t pixel[4]{};
    glReadBuffer(GL_BACK);
    // Win32の装飾付きWindowは要求64pxより広い最小幅を持つことがあります。
    // Window中央ではなく64x64のDock本文内（Headerの下）の論理座標を採取します。
    const int sampleX = 32 * framebufferWidth / width;
    const int sampleY = framebufferHeight - 47 * framebufferHeight / height;
    glReadPixels(sampleX, sampleY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR || pixel[0] != 255u)
    {
        std::cerr << "UI render GL error=" << error << ", pixel=" << static_cast<int>(pixel[0])
            << ',' << static_cast<int>(pixel[1]) << ',' << static_cast<int>(pixel[2]) << '\n';
        for (const auto& command : context.GetDrawList().GetCommands())
        {
            if (command.Color.x == 1.0f && command.Color.y == 0.0f)
            {
                std::cerr << "red rect=" << command.Rect.Min.x << ',' << command.Rect.Min.y
                    << '-' << command.Rect.Max.x << ',' << command.Rect.Max.y << " clip="
                    << command.Clip.Rect.Min.x << ',' << command.Clip.Rect.Min.y << '-'
                    << command.Clip.Rect.Max.x << ',' << command.Clip.Rect.Max.y
                    << " alpha=" << command.Color.w << '\n';
            }
        }
    }
    GLint mode[2]{};
    glGetIntegerv(GL_POLYGON_MODE, mode);
    if (pixel[0] != 255u || mode[0] != GL_LINE)
    {
        std::cerr << "polygon mode=" << mode[0] << " framebuffer="
            << framebufferWidth << ',' << framebufferHeight << '\n';
    }
    return Check(error == GL_NO_ERROR, "UI shared-context render/readback GL error") &&
        Check(mode[0] == GL_LINE, "UI restores Core polygon mode") &&
        Check(pixel[0] == 255u && pixel[1] == 0u && pixel[2] == 0u && pixel[3] == 255u,
            "detached UI produces visible red pixel");
}

bool TestDetachedUIRendering(GLFWwindow* mainWindow, bool enableCache)
{
    // Windowsのdefault framebufferは非表示/遮蔽時のpixelを保証しません。
    // Texture uploadとは異なり、この回帰は実Window描画を確認するので表示して採取します。
    glfwShowWindow(mainWindow);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwPollEvents();
    // 通常Editorと同じShader相対Pathを使います。テスト実行側の作業Dirは終了時に戻します。
    struct WorkingDirectoryScope
    {
        std::filesystem::path Original = std::filesystem::current_path();
        ~WorkingDirectoryScope()
        {
            std::error_code error;
            std::filesystem::current_path(Original, error);
        }
    } directory;
    if (std::filesystem::exists("Raven/Assets/Shaders/Glsl/UI.glsl") == false)
    {
        std::filesystem::current_path(directory.Original / "Root" / "Root");
    }
    Raven::UIContext main;
    main.SetRenderer(Raven::UIRenderer::Create(Raven::RHIBackend::OpenGL));
    auto dock = std::make_unique<Raven::UIDockSpace>();
    auto* dockPtr = dock.get();
    dock->SetSize({ 64.0f, 64.0f });
    const auto leaf = dock->GetLayout().GetRoot()->GetId();
    if (Check(dock->CreateTabView(leaf) != nullptr, "GPU detached pane creation") == false)
    {
        return false;
    }
    auto page = std::make_unique<Raven::UIElement>();
    auto* pagePtr = page.get();
    page->SetHorizontalAlignment(Raven::UIAlignment::Stretch);
    page->SetVerticalAlignment(Raven::UIAlignment::Stretch);
    page->SetClipChildren(true);
    page->SetDrawListCacheEnabled(enableCache);
    auto panel = std::make_unique<Raven::UIPanel>();
    panel->SetAffectsParentMeasure(false);
    panel->SetHorizontalAlignment(Raven::UIAlignment::Stretch);
    panel->SetVerticalAlignment(Raven::UIAlignment::Stretch);
    panel->SetBackgroundColor({ 1.0f, 0.0f, 0.0f, 1.0f });
    page->AddChild(std::move(panel));
    if (Check(dock->AddTab(leaf, 701u, "Detached", std::move(page), false), "GPU detached tab creation") == false)
    {
        return false;
    }
    main.AddRootChild(std::move(dock));
    if (CheckRedFrame(mainWindow, main, enableCache) == false)
    {
        return false;
    }
    // VAOはContext別、Texture/Shader等は共有する通常補助Windowと同じ条件で反復します。
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        GLFWwindow* window = glfwCreateWindow(64, 64, "Raven Auxiliary UI Test", nullptr, mainWindow);
        if (Check(window != nullptr, "GPU auxiliary shared Window") == false)
        {
            return false;
        }
        glfwMakeContextCurrent(window);
        bool success = false;
        {
            // RendererのVAO/VBO/EBOは所属ContextがCurrentな間に破棄します。
            Raven::UIContext auxiliary;
            auxiliary.SetRenderer(Raven::UIRenderer::Create(Raven::RHIBackend::OpenGL));
            Raven::UITabItem metadata;
            auto content = dockPtr->ExtractTabForWindow(leaf, 701u, metadata);
            success = Check(content != nullptr, "GPU tab extraction");
            if (success == true)
            {
                auxiliary.AddRootChild(std::move(content));
                success = CheckRedFrame(window, auxiliary, enableCache);
                if (success == true)
                {
                    glfwSetWindowSize(window, 96, 96);
                    glfwPollEvents();
                    success = CheckRedFrame(window, auxiliary, enableCache);
                }
                auto restored = auxiliary.DetachRootChild(pagePtr);
                success = Check(dockPtr->AddTab(leaf, metadata.Id, metadata.Title,
                    std::move(restored), metadata.Closable), "GPU tab restoration") && success;
            }
        }
        glfwMakeContextCurrent(mainWindow);
        glfwDestroyWindow(window);
        if (success == false || CheckRedFrame(mainWindow, main, enableCache) == false)
        {
            return false;
        }
    }
    std::cout << "[PASS] GPU detached UI shared-context rendering (3 Close/restore cycles)\n";
    std::cout << "[PASS] GPU subtree cache enabled=" << enableCache << '\n';
    return true;
}
} // namespace

// DX12専用起動ではOpenGLを初期化せず、実SwapChainと共有Deviceの寿命を検証します。
bool TestDX12AuxiliaryWindows()
{
    Raven::WindowSpecification specification("DX12 Main UI Test", 480, 320,
        Raven::RHIBackend::DirectX12);
    auto mainWindow = Raven::Window::Create(specification);
    auto auxiliary = Raven::Window::Create(specification);
    if (Check(mainWindow != nullptr && auxiliary != nullptr, "DX12 test Windows") == false)
    {
        return false;
    }
    {
        Raven::DX12SceneContext main;
        Raven::DX12SceneContext child;
        if (Check(main.Init(*mainWindow), "DX12 main Context") == false ||
            Check(child.Init(*auxiliary, &main), "DX12 shared Context") == false ||
            Check(main.GetNativeDevice() == child.GetNativeDevice(), "DX12 same Device") == false)
        {
            return false;
        }
        // Main終了後も共有所有がDevice/Queueを生存させ、補助のFence待機まで保持します。
        main.Shutdown();
        if (Check(child.BeginFrame() == Raven::RHIFrameResult::Success &&
            child.EndFrame() == Raven::RHIFrameResult::Success &&
            child.Present() == Raven::RHIFrameResult::Success,
            "DX12 shared Device survives source Shutdown") == false)
        {
            return false;
        }
    }
    Raven::DX12SceneRuntime runtime;
    Raven::PipelineSpecification pipeline{};
    pipeline.Topology = Raven::PrimitiveTopology::Triangles;
    pipeline.Cull = Raven::CullMode::None;
    pipeline.DepthTest = true;
    pipeline.DepthWrite = true;
    Raven::RHIShaderAssetSpecification vertex{};
    vertex.DirectX12Path = "Raven/Assets/Shaders/DirectX12/SceneMesh.vs.dxil";
    Raven::RHIShaderAssetSpecification fragment{};
    fragment.DirectX12Path = "Raven/Assets/Shaders/DirectX12/SceneMesh.ps.dxil";
    if (Check(runtime.Init(*mainWindow, pipeline, vertex, fragment), "DX12 Runtime Init") == false ||
        Check(runtime.CreateAuxiliaryUIWindow(*mainWindow) == false, "DX12 rejects Main registration") == false)
    {
        return false;
    }
    Raven::UIContext ui;
    auto panel = Raven::CreateScope<Raven::UIPanel>();
    panel->SetSize({ 128.0f, 128.0f });
    panel->SetBackgroundColor({ 1.0f, 0.0f, 0.0f, 1.0f });
    ui.AddRootChild(std::move(panel));
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        if (Check(runtime.CreateAuxiliaryUIWindow(*auxiliary), "DX12 auxiliary registration") == false ||
            Check(runtime.CreateAuxiliaryUIWindow(*auxiliary) == false, "DX12 duplicate registration rejected") == false)
        {
            return false;
        }
        for (int frame = 0; frame < 5; ++frame)
        {
            if (frame == 2)
            {
                glfwSetWindowSize(static_cast<GLFWwindow*>(auxiliary->GetNativeWindow()),
                    640 + cycle * 16, 400 + cycle * 16);
                auxiliary->PollEvents();
            }
            ui.BeginFrame({ static_cast<float>(auxiliary->GetWidth()),
                static_cast<float>(auxiliary->GetHeight()) });
            ui.EndFrame();
            if (Check(runtime.RenderAuxiliaryUIWindow(*auxiliary, ui.GetDrawList()) == Raven::RHIFrameResult::Success,
                "DX12 auxiliary draw/Resize") == false)
            {
                return false;
            }
        }
        runtime.DestroyAuxiliaryUIWindow(*auxiliary);
        runtime.DestroyAuxiliaryUIWindow(*auxiliary); // 再通知でも二重解放しません。
        if (Check(runtime.RenderAuxiliaryUIWindow(*auxiliary, ui.GetDrawList()) == Raven::RHIFrameResult::FatalError,
            "DX12 closed auxiliary rejected") == false)
        {
            return false;
        }
    }
    // Shutdown時にまだ登録された補助がある経路もNative Window破棄前に解放します。
    if (Check(runtime.CreateAuxiliaryUIWindow(*auxiliary), "DX12 final auxiliary registration") == false)
    {
        return false;
    }
    runtime.Shutdown();
    std::cout << "[PASS] DX12 auxiliary shared Device, Resize and Close (3 cycles)\n";
    return true;
}

bool TestVulkanSurfaceFormats()
{
    VkClearColorValue displayClear{};
    displayClear.float32[0] = 0.5f;
    displayClear.float32[1] = 0.01f;
    displayClear.float32[2] = 1.0f;
    displayClear.float32[3] = 0.375f;
    const auto srgbClear = Raven::VulkanSwapChain::PrepareTransferClearValue(VK_FORMAT_B8G8R8A8_SRGB, displayClear);
    const auto unormClear = Raven::VulkanSwapChain::PrepareTransferClearValue(VK_FORMAT_B8G8R8A8_UNORM, displayClear);
    if (Check(std::abs(srgbClear.float32[0] - 0.21404114f) < 0.000002f &&
        std::abs(srgbClear.float32[1] - 0.000773994f) < 0.000002f && srgbClear.float32[2] == 1.0f &&
        srgbClear.float32[3] == 0.375f && unormClear.float32[0] == 0.5f && unormClear.float32[1] == 0.01f &&
        unormClear.float32[3] == 0.375f, "Transfer Clear compensates native sRGB only; alpha unchanged") == false)
    {
        return false;
    }
    VkSurfaceFormatKHR selected{};
    VkFormat attachment = VK_FORMAT_UNDEFINED;
    const auto select = [&](const std::vector<VkSurfaceFormatKHR>& formats, bool mutableFormat)
    {
        return Raven::VulkanSwapChain::SelectSurfaceFormat(formats, mutableFormat, selected, attachment);
    };
    const auto nonlinear = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    if (Check(select({{VK_FORMAT_B8G8R8A8_SRGB, nonlinear}, {VK_FORMAT_R8G8B8A8_UNORM, nonlinear},
        {VK_FORMAT_B8G8R8A8_UNORM, nonlinear}}, true) && selected.format == VK_FORMAT_B8G8R8A8_UNORM &&
        attachment == VK_FORMAT_B8G8R8A8_UNORM, "Surface prefers BGRA UNORM regardless of order") == false ||
        Check(select({{VK_FORMAT_R8G8B8A8_UNORM, nonlinear}}, false) && attachment == VK_FORMAT_R8G8B8A8_UNORM,
            "Surface RGBA UNORM fallback without extensions") == false ||
        Check(select({{VK_FORMAT_B8G8R8A8_SRGB, nonlinear}}, true) && selected.format == VK_FORMAT_B8G8R8A8_SRGB &&
            attachment == VK_FORMAT_B8G8R8A8_UNORM, "sRGB Surface uses mutable UNORM attachment") == false ||
        Check(select({{VK_FORMAT_R8G8B8A8_SRGB, nonlinear}}, true) && attachment == VK_FORMAT_R8G8B8A8_UNORM,
            "RGBA sRGB mutable fallback") == false ||
        Check(select({{VK_FORMAT_B8G8R8A8_SRGB, nonlinear}}, false) == false && attachment == VK_FORMAT_UNDEFINED,
            "sRGB-only unsupported extension fails explicitly") == false ||
        Check(select({}, true) == false, "empty Surface list rejected") == false ||
        Check(select({{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_HDR10_ST2084_EXT}}, true) == false,
            "unsupported HDR color space rejected") == false ||
        Check(select({{VK_FORMAT_A2B10G10R10_UNORM_PACK32, nonlinear}}, true) == false,
            "unsupported 10bit output rejected") == false ||
        Check(select({{VK_FORMAT_UNDEFINED, nonlinear}}, false) && attachment == VK_FORMAT_B8G8R8A8_UNORM,
            "legacy undefined Surface permits UNORM selection") == false)
    {
        return false;
    }
    std::cout << "[PASS] Vulkan SDR Surface format selection\n";
    return true;
}

// 診断BufferはContextより先に破棄します。途中失敗時もGPUの利用完了を待ってから解放します。
class VulkanColorReadback
{
public:
    explicit VulkanColorReadback(const Raven::VulkanDevice& device) : Device(device.GetHandle()) {}
    VulkanColorReadback(const VulkanColorReadback&) = delete;
    VulkanColorReadback& operator=(const VulkanColorReadback&) = delete;
    ~VulkanColorReadback()
    {
        vkDeviceWaitIdle(Device);
        if (Buffer != VK_NULL_HANDLE) { vkDestroyBuffer(Device, Buffer, nullptr); }
        if (Memory != VK_NULL_HANDLE) { vkFreeMemory(Device, Memory, nullptr); }
    }
    bool Init(VkPhysicalDevice physical, VkDeviceSize size)
    {
        Size = size;
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Device, &info, nullptr, &Buffer) != VK_SUCCESS) { return false; }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(Device, Buffer, &requirements);
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            const auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            if ((requirements.memoryTypeBits & (1u << index)) != 0 &&
                (properties.memoryTypes[index].propertyFlags & flags) == flags)
            {
                VkMemoryAllocateInfo allocation{};
                allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocation.allocationSize = requirements.size;
                allocation.memoryTypeIndex = index;
                return vkAllocateMemory(Device, &allocation, nullptr, &Memory) == VK_SUCCESS &&
                    vkBindBufferMemory(Device, Buffer, Memory, 0) == VK_SUCCESS;
            }
        }
        return false;
    }
    bool CheckPixel(const void* pixels, uint32_t width, uint32_t x, uint32_t y, bool bgra,
        const std::array<int, 4>& expected, const char* label) const
    {
        const auto* pixel = static_cast<const std::uint8_t*>(pixels) + (static_cast<std::size_t>(y) * width + x) * 4u;
        const std::array<int, 4> actual = {pixel[bgra ? 2 : 0], pixel[1], pixel[bgra ? 0 : 2], pixel[3]};
        for (std::size_t channel = 0; channel < actual.size(); ++channel)
        {
            if (std::abs(actual[channel] - expected[channel]) > 2)
            {
                std::cerr << "[FAIL] " << label << " channel=" << channel << " actual=" << actual[channel]
                    << " expected=" << expected[channel] << '\n';
                return false;
            }
        }
        std::cout << "[PASS] " << label << " RGBA=" << actual[0] << ',' << actual[1] << ',' << actual[2] << ',' << actual[3] << '\n';
        return true;
    }
    VkDevice Device;
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
};

bool CheckVulkanColorFrame(Raven::VulkanSceneContext& context)
{
    Raven::VulkanSceneRHIDevice device(context);
    Raven::VulkanSceneCommandList commands(context);
    Raven::RHIShaderAssetManager shaders;
    auto vertex = shaders.Load("Raven/Assets/Shaders/Vulkan/UI.vert.spv", Raven::RHIBackend::Vulkan);
    auto fragment = shaders.Load("Raven/Assets/Shaders/Vulkan/UI.frag.spv", Raven::RHIBackend::Vulkan);
    Raven::Ref<Raven::RHIGraphicsPipeline> pipeline;
    if (Check(vertex != nullptr && fragment != nullptr &&
        Raven::ExplicitUIRenderer::CreatePipeline(device, vertex->GetBinary(), fragment->GetBinary(), pipeline),
        "Vulkan reference color pipeline") == false) { return false; }
    Raven::RHITextureSpecification whiteSpecification{};
    whiteSpecification.GenerateMips = false;
    const std::uint8_t white[] = {255, 255, 255, 255};
    auto texture = device.CreateTexture(whiteSpecification, white, sizeof(white));
    Raven::RHIOffscreenRenderTargetSpecification targetSpecification{};
    targetSpecification.Width = 16;
    targetSpecification.Height = 16;
    targetSpecification.ClearColor = {0.2f, 0.4f, 0.6f, 1.0f};
    auto target = device.CreateOffscreenRenderTarget(targetSpecification);
    if (Check(texture != nullptr && target != nullptr, "Vulkan reference color textures") == false) { return false; }
    Raven::UIDrawList list;
    list.AddRect({16.0f, 16.0f}, {64.0f, 64.0f}, {0.25f, 0.5f, 0.75f, 1.0f});
    list.AddRect({80.0f, 16.0f}, {128.0f, 64.0f}, {1.0f, 0.0f, 0.0f, 0.5f});
    list.AddImage({144.0f, 16.0f}, {192.0f, 64.0f}, Raven::UITextureView::FromRHITexture(target->GetColorAttachment()));
    const auto extent = context.GetExtent();
    Raven::ExplicitUIRenderer renderer;
    Raven::PreparedExplicitUI prepared;
    const float background[] = {0.1f, 0.2f, 0.3f, 1.0f};
    context.SetClearColor(background);
    VulkanColorReadback readback(context.GetDevice());
    if (Check(renderer.Prepare(device, list, extent.width, extent.height, texture, pipeline, prepared) &&
        readback.Init(context.GetDevice().GetPhysicalDeviceHandle(), static_cast<VkDeviceSize>(extent.width) * extent.height * 4u),
        "Vulkan reference color preparation") == false ||
        Check(context.CopyColorToBuffer(readback.Buffer, readback.Size) == false,
            "Vulkan readback outside Frame rejected") == false ||
        Check(context.BeginFrame() == Raven::RHIFrameResult::Success &&
            context.CopyColorToBuffer(VK_NULL_HANDLE, readback.Size) == false &&
            context.CopyColorToBuffer(readback.Buffer, readback.Size - 1u) == false &&
            context.BeginOffscreenRenderTarget(target, targetSpecification.ClearColor.data()) &&
            context.CopyColorToBuffer(readback.Buffer, readback.Size) == false &&
            context.EndOffscreenRenderTarget(target) && renderer.Draw(commands, pipeline, prepared) &&
            context.CopyColorToBuffer(readback.Buffer, readback.Size) &&
            context.EndFrame() == Raven::RHIFrameResult::Success &&
            context.Present() != Raven::RHIFrameResult::FatalError && context.GetDevice().WaitIdle(),
            "Vulkan reference draw/copy/Present") == false) { return false; }
    void* pixels = nullptr;
    if (Check(vkMapMemory(readback.Device, readback.Memory, 0, readback.Size, 0, &pixels) == VK_SUCCESS,
        "Vulkan color readback map") == false) { return false; }
    const bool bgra = context.GetColorFormat() == VK_FORMAT_B8G8R8A8_UNORM;
    const bool matches = readback.CheckPixel(pixels, extent.width, 8, 8, bgra, {26, 51, 77, 255}, "Scene clear color") &&
        readback.CheckPixel(pixels, extent.width, 32, 32, bgra, {64, 128, 191, 255}, "UI opaque reference") &&
        readback.CheckPixel(pixels, extent.width, 96, 32, bgra, {140, 26, 38, 191}, "UI half alpha blend (OpenGL-compatible Alpha)") &&
        readback.CheckPixel(pixels, extent.width, 160, 32, bgra, {51, 102, 153, 255}, "Viewport texture sampled once");
    vkUnmapMemory(readback.Device, readback.Memory);
    return matches;
}

bool TestVulkanColorOutput()
{
    if (TestVulkanSurfaceFormats() == false) { return false; }
    Raven::WindowSpecification specification("Vulkan Color Output Test", 320, 240, Raven::RHIBackend::Vulkan);
    auto mainWindow = Raven::Window::Create(specification);
    auto childWindow = Raven::Window::Create(specification);
    if (Check(mainWindow != nullptr && childWindow != nullptr, "Vulkan color Windows") == false) { return false; }
    Raven::VulkanSceneContext main;
    Raven::VulkanSceneContext child;
    if (Check(main.Init(*mainWindow), "Vulkan UNORM color Context") == false ||
        Check(main.GetSurfaceFormat() == main.GetColorFormat(), "Vulkan default native UNORM Surface") == false ||
        Check(CheckVulkanColorFrame(main), "Vulkan UNORM color bytes") == false) { return false; }
    if (Check(main.GetDevice().SupportsMutableSwapChainFormat(), "Vulkan mutable fallback capability required for this test") == false ||
        Check(child.Init(*childWindow, &main, VK_FORMAT_B8G8R8A8_SRGB), "Vulkan forced sRGB Surface fallback") == false ||
        Check(child.GetSurfaceFormat() == VK_FORMAT_B8G8R8A8_SRGB && child.GetColorFormat() == VK_FORMAT_B8G8R8A8_UNORM,
            "Vulkan sRGB native/UNORM attachment separation") == false ||
        Check(CheckVulkanColorFrame(child), "Vulkan sRGB fallback color bytes") == false) { return false; }
    // 再生成後もSurfaceの選択方針・View/RenderPass/Pipelineの形式が一致することを確認します。
    childWindow->SetSize(400, 300);
    childWindow->PollEvents();
    if (Check(child.Resize(childWindow->GetFramebufferWidth(), childWindow->GetFramebufferHeight()) &&
        child.GetSurfaceFormat() == VK_FORMAT_B8G8R8A8_SRGB && child.GetColorFormat() == VK_FORMAT_B8G8R8A8_UNORM &&
        CheckVulkanColorFrame(child), "Vulkan fallback color after Resize") == false) { return false; }
    std::cout << "[PASS] Vulkan UNORM/sRGB mutable output, clear/opaque/blend/Viewport readback and Resize\n";
    return true;
}

bool TestVulkanAuxiliaryWindows()
{
    Raven::WindowSpecification specification("Vulkan Auxiliary UI Test", 480, 320,
        Raven::RHIBackend::Vulkan);
    auto mainWindow = Raven::Window::Create(specification);
    auto auxiliary = Raven::Window::Create(specification);
    auto second = Raven::Window::Create(specification);
    if (Check(mainWindow != nullptr && auxiliary != nullptr && second != nullptr,
        "Vulkan test Windows") == false)
    {
        return false;
    }
    {
        Raven::VulkanSceneContext main;
        Raven::VulkanSceneContext child;
        if (Check(main.Init(*mainWindow), "Vulkan main Context") == false ||
            Check(child.Init(*auxiliary, &main), "Vulkan shared Context") == false ||
            Check(main.GetDevice().GetHandle() == child.GetDevice().GetHandle(),
                "Vulkan same Device") == false ||
            Check(main.GetDevice().GetGraphicsQueue() == child.GetDevice().GetGraphicsQueue(),
                "Vulkan same Queue") == false)
        {
            return false;
        }
        main.Shutdown();
        if (Check(child.BeginFrame() == Raven::RHIFrameResult::Success &&
            child.EndFrame() == Raven::RHIFrameResult::Success &&
            child.Present() != Raven::RHIFrameResult::FatalError,
            "Vulkan shared Instance survives source Shutdown") == false)
        {
            return false;
        }
    }
    Raven::VulkanSceneRuntime runtime;
    Raven::PipelineSpecification pipeline{};
    pipeline.Topology = Raven::PrimitiveTopology::Triangles;
    pipeline.Cull = Raven::CullMode::None;
    pipeline.DepthTest = true;
    pipeline.DepthWrite = true;
    Raven::RHIShaderAssetSpecification vertex{};
    vertex.VulkanPath = "Raven/Assets/Shaders/Vulkan/SceneTriangle.vert.spv";
    Raven::RHIShaderAssetSpecification fragment{};
    fragment.VulkanPath = "Raven/Assets/Shaders/Vulkan/SceneTriangle.frag.spv";
    if (Check(runtime.Init(*mainWindow, pipeline, vertex, fragment), "Vulkan Runtime Init") == false ||
        Check(runtime.CreateAuxiliaryUIWindow(*mainWindow) == false, "Vulkan rejects Main registration") == false ||
        Check(runtime.CreateAuxiliaryUIWindow(*second), "Vulkan second auxiliary") == false)
    {
        return false;
    }
    // Mainが書いたoff-screen Colorを、二つのSurfaceで共有してSamplingします。
    Raven::RHIOffscreenRenderTargetSpecification targetSpecification{};
    targetSpecification.Width = 64;
    targetSpecification.Height = 64;
    targetSpecification.ClearColor = { 1.0f, 0.0f, 0.0f, 1.0f };
    auto target = runtime.CreateOffscreenRenderTarget(targetSpecification);
    auto* context = static_cast<Raven::VulkanSceneContext*>(runtime.GetFrameLifecycle());
    if (Check(target != nullptr && context != nullptr, "Vulkan shared Viewport target") == false)
    {
        return false;
    }
    Raven::UIDrawList drawList;
    drawList.AddImage({ 0.0f, 0.0f }, { 128.0f, 128.0f },
        Raven::UITextureView::FromRHITexture(target->GetColorAttachment()));
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        if (cycle > 0)
        {
            // 二つ目の補助が旧ColorのDescriptorを持ったまま、Main RenderPass/Targetを交換。
            // 次のPrepareで無効化済みImageViewを再利用せず、新しいColorへ切り替えます。
            if (Check(runtime.Resize(mainWindow->GetFramebufferWidth(), mainWindow->GetFramebufferHeight()),
                "Vulkan Main Resize with live auxiliary") == false)
            {
                return false;
            }
            target = runtime.CreateOffscreenRenderTarget(targetSpecification);
            if (Check(target != nullptr, "Vulkan replacement Viewport") == false)
            {
                return false;
            }
            drawList.Clear();
            drawList.AddImage({ 0.0f, 0.0f }, { 128.0f, 128.0f },
                Raven::UITextureView::FromRHITexture(target->GetColorAttachment()));
        }
        if (Check(runtime.CreateAuxiliaryUIWindow(*auxiliary), "Vulkan auxiliary registration") == false ||
            Check(runtime.CreateAuxiliaryUIWindow(*auxiliary) == false,
                "Vulkan duplicate registration rejected") == false)
        {
            return false;
        }
        int completed = 0;
        for (int frame = 0; frame < 10; ++frame)
        {
            if (frame == 2)
            {
                glfwSetWindowSize(static_cast<GLFWwindow*>(auxiliary->GetNativeWindow()),
                    640 + cycle * 16, 400 + cycle * 16);
                auxiliary->PollEvents();
            }
            if (Check(context->BeginFrame() == Raven::RHIFrameResult::Success &&
                context->BeginOffscreenRenderTarget(target, targetSpecification.ClearColor.data()) &&
                context->EndOffscreenRenderTarget(target) &&
                context->EndFrame() == Raven::RHIFrameResult::Success &&
                context->Present() != Raven::RHIFrameResult::FatalError,
                "Vulkan Main writes shared Viewport") == false)
            {
                return false;
            }
            const auto firstResult = runtime.RenderAuxiliaryUIWindow(*auxiliary, drawList);
            const auto secondResult = runtime.RenderAuxiliaryUIWindow(*second, drawList);
            if (Check(firstResult != Raven::RHIFrameResult::FatalError &&
                secondResult != Raven::RHIFrameResult::FatalError,
                "Vulkan shared Viewport Sampling/Resize") == false)
            {
                return false;
            }
            if (firstResult == Raven::RHIFrameResult::Success &&
                secondResult == Raven::RHIFrameResult::Success)
            {
                ++completed;
            }
        }
        if (Check(completed >= 3, "Vulkan completed auxiliary frames after Resize") == false)
        {
            return false;
        }
        runtime.DestroyAuxiliaryUIWindow(*auxiliary);
        runtime.DestroyAuxiliaryUIWindow(*auxiliary);
        if (Check(runtime.RenderAuxiliaryUIWindow(*auxiliary, drawList) ==
            Raven::RHIFrameResult::FatalError, "Vulkan closed auxiliary rejected") == false)
        {
            return false;
        }
    }
    runtime.Shutdown(); // 登録済みsecondと共有Texture/RenderPassもNative Windowより先に解放。
    std::cout << "[PASS] Vulkan auxiliary shared Viewport, Resize and Close (3 cycles)\n";
    return true;
}

namespace
{
using ParityPixels = std::vector<std::uint8_t>;

// API間ではPixel中心が辺上に重なる場合の微小丸め/充填規約が異なり得ます。
// 任意の1 Pixel幅を許すのではなく、共有Geometryの辺に中心がほぼ一致する点だけを区別します。
bool IsUIRasterBoundary(uint32_t x, uint32_t y, const Raven::UIRenderGeometry& geometry,
    const Raven::math::Vec2& logical, uint32_t width, uint32_t height)
{
    const double px = x + 0.5, py = y + 0.5;
    for (const auto& batch : geometry.Batches)
    {
        if (x < batch.Clip.X || y < batch.Clip.Y || x >= batch.Clip.X + batch.Clip.Width || y >= batch.Clip.Y + batch.Clip.Height)
        {
            continue;
        }
        for (uint32_t i = batch.FirstIndex; i < batch.FirstIndex + batch.IndexCount; i += 3u)
        {
            for (uint32_t edge = 0; edge < 3u; ++edge)
            {
                const auto& a = geometry.Vertices[geometry.Indices[i + edge]].Position;
                const auto& b = geometry.Vertices[geometry.Indices[i + (edge + 1u) % 3u]].Position;
                const double ax = static_cast<double>(a.x) * width / logical.x;
                const double ay = static_cast<double>(a.y) * height / logical.y;
                const double dx = static_cast<double>(b.x - a.x) * width / logical.x;
                const double dy = static_cast<double>(b.y - a.y) * height / logical.y;
                const double squared = dx * dx + dy * dy;
                const double projection = (px - ax) * dx + (py - ay) * dy;
                constexpr double pixelEpsilon = 0.0001;
                if (squared > 0.0 && projection >= 0.0 && projection <= squared &&
                    std::abs((px - ax) * dy - (py - ay) * dx) <= pixelEpsilon * std::sqrt(squared))
                {
                    return true;
                }
            }
        }
    }
    return false;
}

bool CompareUIReadback(const ParityPixels& reference, const ParityPixels& actual, const char* backend,
    const Raven::UIRenderGeometry& geometry, const Raven::math::Vec2& logical, uint32_t width, uint32_t height)
{
    if (reference.size() != actual.size()) { return Check(false, "UI parity image dimensions"); }
    std::size_t interiors = 0u, boundaries = 0u;
    int maximum = 0;
    for (std::size_t i = 0; i < reference.size(); i += 4u)
    {
        int delta = 0;
        for (std::size_t channel = 0; channel < 4u; ++channel)
        {
            delta = std::max(delta, std::abs(static_cast<int>(reference[i + channel]) - actual[i + channel]));
        }
        maximum = std::max(maximum, delta);
        if (delta > 3)
        {
            const uint32_t x = static_cast<uint32_t>(i / 4u % width);
            const uint32_t y = static_cast<uint32_t>(i / 4u / width);
            if (IsUIRasterBoundary(x, y, geometry, logical, width, height) == true)
            {
                ++boundaries;
            }
            else
            {
                if (interiors < 3u) { std::cout << "UI parity non-boundary difference " << x << ',' << y << " error=" << delta << '\n'; }
                ++interiors;
            }
        }
    }
    std::cout << "UI parity " << backend << " interior_pixels_over_3=" << interiors
        << " exact_edge_ties=" << boundaries << " max_error=" << maximum << '\n';
    return Check(interiors == 0u, "OpenGL/RHI RGBA parity except exact shared-geometry edge ties");
}

bool ReadVulkanUI(const Raven::UIDrawList& list, const Raven::math::Vec2& logical,
    uint32_t width, uint32_t height, ParityPixels& pixels)
{
    Raven::WindowSpecification specification("M5 Vulkan UI parity", width, height, Raven::RHIBackend::Vulkan);
    auto window = Raven::Window::Create(specification);
    Raven::VulkanSceneContext context;
    if (window == nullptr || context.Init(*window) == false) { return Check(false, "Vulkan parity Context"); }
    Raven::VulkanSceneRHIDevice device(context);
    Raven::VulkanSceneCommandList commands(context);
    Raven::RHIShaderAssetManager shaders;
    auto vertex = shaders.Load("Raven/Assets/Shaders/Vulkan/UI.vert.spv", Raven::RHIBackend::Vulkan);
    auto fragment = shaders.Load("Raven/Assets/Shaders/Vulkan/UI.frag.spv", Raven::RHIBackend::Vulkan);
    Raven::Ref<Raven::RHIGraphicsPipeline> pipeline;
    if (vertex == nullptr || fragment == nullptr || Raven::ExplicitUIRenderer::CreatePipeline(device,
        vertex->GetBinary(), fragment->GetBinary(), pipeline) == false) { return Check(false, "Vulkan parity pipeline"); }
    Raven::RHITextureSpecification textureSpecification{};
    textureSpecification.GenerateMips = false;
    const std::uint8_t white[] = {255, 255, 255, 255};
    auto whiteTexture = device.CreateTexture(textureSpecification, white, sizeof(white));
    Raven::ExplicitUIRenderer renderer;
    Raven::PreparedExplicitUI prepared;
    const auto extent = context.GetExtent();
    VulkanColorReadback readback(context.GetDevice());
    if (extent.width != width || extent.height != height ||
        renderer.Prepare(device, list, logical, width, height, whiteTexture, pipeline, prepared) == false ||
        readback.Init(context.GetDevice().GetPhysicalDeviceHandle(), static_cast<VkDeviceSize>(width) * height * 4u) == false ||
        context.BeginFrame() != Raven::RHIFrameResult::Success || renderer.Draw(commands, pipeline, prepared) == false ||
        context.CopyColorToBuffer(readback.Buffer, readback.Size) == false ||
        context.EndFrame() != Raven::RHIFrameResult::Success || context.Present() == Raven::RHIFrameResult::FatalError ||
        context.GetDevice().WaitIdle() == false) { return Check(false, "Vulkan parity render/readback"); }
    void* mapped = nullptr;
    if (vkMapMemory(readback.Device, readback.Memory, 0, readback.Size, 0, &mapped) != VK_SUCCESS) { return false; }
    pixels.assign(static_cast<std::uint8_t*>(mapped), static_cast<std::uint8_t*>(mapped) + width * height * 4u);
    vkUnmapMemory(readback.Device, readback.Memory);
    if (context.GetColorFormat() == VK_FORMAT_B8G8R8A8_UNORM)
    {
        for (std::size_t i = 0; i < pixels.size(); i += 4u) { std::swap(pixels[i], pixels[i + 2u]); }
    }
    return true;
}

bool ReadDX12UI(const Raven::UIDrawList& list, const Raven::math::Vec2& logical,
    uint32_t width, uint32_t height, ParityPixels& pixels)
{
    Raven::WindowSpecification specification("M5 DX12 UI parity", width, height, Raven::RHIBackend::DirectX12);
    auto window = Raven::Window::Create(specification);
    Raven::DX12SceneContext context;
    if (window == nullptr || context.Init(*window) == false) { return Check(false, "DX12 parity Context"); }
    Raven::DX12SceneRHIDevice device(context);
    Raven::DX12SceneCommandList commands(context);
    Raven::RHIShaderAssetManager shaders;
    Raven::RHIShaderAssetSpecification vs{}, fs{};
    vs.DirectX12Path = "Raven/Assets/Shaders/DirectX12/UI.vs.dxil"; vs.EntryPoint = "VSMain";
    fs.DirectX12Path = "Raven/Assets/Shaders/DirectX12/UI.ps.dxil"; fs.EntryPoint = "PSMain";
    auto vertex = shaders.Load(vs, Raven::RHIBackend::DirectX12);
    auto fragment = shaders.Load(fs, Raven::RHIBackend::DirectX12);
    Raven::Ref<Raven::RHIGraphicsPipeline> pipeline;
    if (vertex == nullptr || fragment == nullptr || Raven::ExplicitUIRenderer::CreatePipeline(device,
        vertex->GetBinary(), fragment->GetBinary(), pipeline) == false) { return Check(false, "DX12 parity pipeline"); }
    Raven::RHITextureSpecification textureSpecification{};
    textureSpecification.GenerateMips = false;
    const std::uint8_t white[] = {255, 255, 255, 255};
    auto whiteTexture = device.CreateTexture(textureSpecification, white, sizeof(white));
    Raven::RHIOffscreenRenderTargetSpecification targetSpecification{};
    targetSpecification.Width = width; targetSpecification.Height = height;
    targetSpecification.ClearColor = {0.0f, 0.0f, 0.0f, 1.0f};
    auto target = device.CreateOffscreenRenderTarget(targetSpecification);
    auto native = std::dynamic_pointer_cast<Raven::DX12OffscreenRenderTarget>(target);
    Raven::ExplicitUIRenderer renderer;
    Raven::PreparedExplicitUI prepared;
    if (native == nullptr || renderer.Prepare(device, list, logical, width, height,
        whiteTexture, pipeline, prepared) == false) { return Check(false, "DX12 parity prepare"); }
    auto* nativeDevice = context.GetNativeDevice();
    const auto description = native->GetColorResource()->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 size = 0;
    nativeDevice->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &size);
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = size;
    buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    if (FAILED(nativeDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))) ||
        context.BeginFrame() != Raven::RHIFrameResult::Success ||
        context.BeginOffscreenRenderTarget(target, targetSpecification.ClearColor.data()) == false ||
        renderer.Draw(commands, pipeline, prepared) == false || context.EndOffscreenRenderTarget(target) == false)
    { return Check(false, "DX12 parity draw"); }
    // 計測専用Readback。既存公開native境界を借用し、Resourceを元のSampling状態へ戻します。
    // ContextのFrame Fenceが終わるまでreadback/target/preparedをこのStackで保持します。
    auto* cmd = context.GetActiveCommandList();
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = native->GetColorResource();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cmd->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = native->GetColorResource(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = readback.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    cmd->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    cmd->ResourceBarrier(1, &barrier);
    if (context.EndFrame() != Raven::RHIFrameResult::Success || context.Present() == Raven::RHIFrameResult::FatalError ||
        context.Resize(width, height) == false) { return Check(false, "DX12 parity Fence/Resize"); }
    // Resizeは旧FrameのFence完了を待つ既存契約なので、ここからCPUで読み取れます。
    void* mapped = nullptr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(size)};
    if (FAILED(readback->Map(0, &range, &mapped))) { return false; }
    pixels.resize(static_cast<std::size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y)
    {
        std::memcpy(pixels.data() + static_cast<std::size_t>(y) * width * 4u,
            static_cast<std::uint8_t*>(mapped) + footprint.Offset + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch, width * 4u);
    }
    D3D12_RANGE noWrites{0, 0}; readback->Unmap(0, &noWrites);
    return true;
}

bool TestUIGeometryParity()
{
    if (glfwInit() == GLFW_FALSE) { return false; }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto window = std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>(
        glfwCreateWindow(320, 240, "M5 OpenGL parity", nullptr, nullptr), glfwDestroyWindow);
    if (window == nullptr) { return false; }
    glfwMakeContextCurrent(window.get());
    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0 ||
        Raven::RenderCommand::TryInit(Raven::RHIBackend::OpenGL) == false) { return false; }
    // FontのCPU Pixelを同一Assetとして各Backendへ渡し、Rasterizer差を比較から除きます。
    Raven::UIFontAtlas font;
    Raven::UIFontAtlasBuildOptions fontOptions;
    fontOptions.PixelHeight = 18.0f; fontOptions.AtlasWidth = 128u; fontOptions.AtlasHeight = 128u;
    std::string fontPath = "C:/Windows/Fonts/arial.ttf";
#if defined(_MSC_VER)
    char* configuredFont = nullptr;
    size_t configuredLength = 0u;
    if (_dupenv_s(&configuredFont, &configuredLength, "RAVEN_UI_TEST_FONT") == 0 && configuredFont != nullptr && configuredFont[0] != '\0')
    {
        fontPath = configuredFont;
    }
    std::free(configuredFont);
#else
    const char* configuredFont = std::getenv("RAVEN_UI_TEST_FONT");
    if (configuredFont != nullptr && configuredFont[0] != '\0')
    {
        fontPath = configuredFont;
    }
#endif
    Raven::SetRHIBackend(Raven::RHIBackend::Vulkan);
    const bool built = Raven::UIFontAtlasBuilder::BuildFromFile(fontPath,
        {'M', '5', ' ', 'C', 'l', 'i', 'p'}, fontOptions, font);
    Raven::SetRHIBackend(Raven::RHIBackend::OpenGL);
    if (Check(built && font.GetTexture()->FinalizeRuntimeTexture(), "UI parity real Font Atlas") == false) { return false; }
    Raven::UIDrawList list;
    list.AddRect({4.0f, 4.0f}, {100.0f, 32.0f}, {1.0f, 0.0f, 0.0f, 0.5f});
    list.AddRect({20.0f, 10.0f}, {120.0f, 38.0f}, {0.0f, 1.0f, 0.0f, 0.5f});
    list.ApplyClip(0u, Raven::UIClipRect::FromRect({{8.2f, 6.3f}, {105.1f, 35.1f}}));
    const std::size_t textStart = list.GetCommandCount();
    font.AppendText(list, "M5 Clip", {6.0f, 57.0f}, 20.0f);
    if (Check(list.GetCommandCount() > textStart + 3u, "parity emits real glyph quads") == false) { return false; }
    Raven::UISvg svg;
    if (Check(svg.LoadFromFile("Raven/Assets/Svg/AnimatedRect.svg"), "parity loads SVG subset") == false) { return false; }
    svg.Stop(); svg.SetPosition({0.0f, 64.0f}); svg.SetSize({150.0f, 56.0f}); svg.BuildDrawList(list);
    if (Check(list.GetCommandCount() > textStart + 10u, "parity emits SVG geometry") == false) { return false; }
    struct Case { uint32_t Width; uint32_t Height; float Scale; };
    for (const Case test : {Case{320, 240, 1.0f}, Case{400, 300, 1.25f}, Case{320, 240, 2.0f}})
    {
        glfwMakeContextCurrent(window.get());
        // Window生成がLegacy Factory用Backendを切り替えるため、比較の各GL入力で明示復帰します。
        Raven::SetRHIBackend(Raven::RHIBackend::OpenGL);
        glfwSetWindowSize(window.get(), test.Width, test.Height); glfwPollEvents();
        const Raven::math::Vec2 logical(static_cast<float>(test.Width) / test.Scale, static_cast<float>(test.Height) / test.Scale);
        ParityPixels bottom(static_cast<std::size_t>(test.Width) * test.Height * 4u), reference(bottom.size());
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, test.Width, test.Height);
        glDisable(GL_SCISSOR_TEST); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        {
            Raven::OpenGLUIRenderer renderer;
            renderer.Render(list, logical, {static_cast<float>(test.Width), static_cast<float>(test.Height)});
        }
        glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadBuffer(GL_BACK);
        glReadPixels(0, 0, test.Width, test.Height, GL_RGBA, GL_UNSIGNED_BYTE, bottom.data());
        for (uint32_t y = 0; y < test.Height; ++y)
        {
            std::memcpy(reference.data() + static_cast<std::size_t>(y) * test.Width * 4u,
                bottom.data() + static_cast<std::size_t>(test.Height - y - 1u) * test.Width * 4u, test.Width * 4u);
        }
        const auto nonzero = std::count_if(reference.begin(), reference.end(), [](std::uint8_t value) { return value != 0u && value != 255u; });
        if (Check(nonzero > 1000, "parity reference contains rendered content") == false) { return false; }
        Raven::UIRenderGeometry geometry;
        if (Raven::UIGeometryBuilder::Build(list, logical, test.Width, test.Height, geometry) == false) { return false; }
        ParityPixels vulkan, dx12;
        if (ReadVulkanUI(list, logical, test.Width, test.Height, vulkan) == false ||
            CompareUIReadback(reference, vulkan, "Vulkan", geometry, logical, test.Width, test.Height) == false ||
            ReadDX12UI(list, logical, test.Width, test.Height, dx12) == false ||
            CompareUIReadback(reference, dx12, "DX12", geometry, logical, test.Width, test.Height) == false) { return false; }
        std::cout << "[PASS] M5 Text/SVG/alpha/Clip UI parity scale=" << test.Scale << " pixels=" << test.Width << 'x' << test.Height << '\n';
    }
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--ui-geometry-parity")
    {
        return TestUIGeometryParity() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc == 2 && std::string(argv[1]) == "--vulkan-surface-formats")
    {
        return TestVulkanSurfaceFormats() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc == 2 && std::string(argv[1]) == "--vulkan-color")
    {
        return TestVulkanColorOutput() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc == 2 && std::string(argv[1]) == "--vulkan-auxiliary")
    {
        return TestVulkanAuxiliaryWindows() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc == 2 && std::string(argv[1]) == "--dx12-auxiliary")
    {
        return TestDX12AuxiliaryWindows() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (glfwInit() == GLFW_FALSE)
    {
        std::cerr << "[FAIL] GLFW initialization (OpenGL display required)\n";
        return EXIT_FAILURE;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Raven GPU Texture Test", nullptr, nullptr);
    if (window == nullptr)
    {
        std::cerr << "[FAIL] OpenGL Context creation\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwMakeContextCurrent(window);
    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0)
    {
        std::cerr << "[FAIL] GLAD initialization\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    const bool initialized = Raven::RenderCommand::TryInit(Raven::RHIBackend::OpenGL);
    const bool success = initialized == true && TestUpload() && TestFontAtlas() && TestDetachedUIRendering(window, false) && TestDetachedUIRendering(window, true);
    // GPU ResourceはContextが生きている間に解放します。
    glfwDestroyWindow(window);
    glfwTerminate();
    if (success == true)
    {
        std::cout << "[PASS] GPU Texture upload diagnostics\n";
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
