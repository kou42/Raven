// GPU Contextを作成してTexture upload診断を検証する独立した統合テストです。
// GLFW / GLAD / Raven本体をリンクし、描画可能なOpenGL環境で実行してください。
// 通常のUITextReflowTestsはGPU不要のまま維持します。
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHITexture.h"
#include "Raven/UI/Text/UIFontAtlasBuilder.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

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
    glEnable(GL_TEXTURE_2D); // Core ProfileではINVALID_ENUMになるため、下の明示的な生成を使います。
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
    const char* configured = std::getenv("RAVEN_UI_TEST_FONT");
    if (configured == nullptr || configured[0] == '\0')
    {
        std::cout << "[SKIP] GPU Font Atlas: set RAVEN_UI_TEST_FONT to a TTF/TTC path\n";
        return true;
    }
    if (Check(std::filesystem::is_regular_file(configured), "configured GPU test font exists") == false)
    {
        return false;
    }
    Raven::UIFontAtlas atlas;
    Raven::UIFontAtlasBuildOptions options{};
    options.AtlasWidth = 128u;
    options.AtlasHeight = 128u;
    Raven::UIFontAtlasBuildFailure failure = Raven::UIFontAtlasBuildFailure::TextureUploadFailed;
    return Check(Raven::UIFontAtlasBuilder::BuildFromFile(
            configured, { 65u }, options, atlas, &failure), "GPU Font Atlas build") &&
        Check(failure == Raven::UIFontAtlasBuildFailure::None, "GPU Font Atlas diagnostic");
}
} // namespace

int main()
{
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
    const bool success = initialized == true && TestUpload() && TestFontAtlas();
    // GPU ResourceはContextが生きている間に解放します。
    glfwDestroyWindow(window);
    glfwTerminate();
    if (success == true)
    {
        std::cout << "[PASS] GPU Texture upload diagnostics\n";
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
