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

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

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

bool CheckRedFrame(GLFWwindow* window, Raven::UIContext& context)
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
    context.BeginFrame({ static_cast<float>(width), static_cast<float>(height) },
        { static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight) }, 1.0f, 1.0f);
    context.EndFrame();
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

bool TestDetachedUIRendering(GLFWwindow* mainWindow)
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
    if (CheckRedFrame(mainWindow, main) == false)
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
                success = CheckRedFrame(window, auxiliary);
                if (success == true)
                {
                    glfwSetWindowSize(window, 96, 96);
                    glfwPollEvents();
                    success = CheckRedFrame(window, auxiliary);
                }
                auto restored = auxiliary.DetachRootChild(pagePtr);
                success = Check(dockPtr->AddTab(leaf, metadata.Id, metadata.Title,
                    std::move(restored), metadata.Closable), "GPU tab restoration") && success;
            }
        }
        glfwMakeContextCurrent(mainWindow);
        glfwDestroyWindow(window);
        if (success == false || CheckRedFrame(mainWindow, main) == false)
        {
            return false;
        }
    }
    std::cout << "[PASS] GPU detached UI shared-context rendering (3 Close/restore cycles)\n";
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
            if (Check(runtime.RenderAuxiliaryUIWindow(*auxiliary, ui.GetDrawList()),
                "DX12 auxiliary draw/Resize") == false)
            {
                return false;
            }
        }
        runtime.DestroyAuxiliaryUIWindow(*auxiliary);
        runtime.DestroyAuxiliaryUIWindow(*auxiliary); // 再通知でも二重解放しません。
        if (Check(runtime.RenderAuxiliaryUIWindow(*auxiliary, ui.GetDrawList()) == false,
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

int main(int argc, char** argv)
{
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
    const bool success = initialized == true && TestUpload() && TestFontAtlas() && TestDetachedUIRendering(window);
    // GPU ResourceはContextが生きている間に解放します。
    glfwDestroyWindow(window);
    glfwTerminate();
    if (success == true)
    {
        std::cout << "[PASS] GPU Texture upload diagnostics\n";
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
