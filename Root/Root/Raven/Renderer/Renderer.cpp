#include "Renderer.h"

#include <iostream>
#include <string_view>

#include "RenderCommand.h"
#include "Raven/Animation/Debug/AnimationDebugOverlayRenderer.h"
#include "Raven/Core/CPUProfiler.h"
#include "Raven/Renderer/Camera/Camera.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"

#include "Raven/Platform/OpenGL/OpenGLRendererAPI.h"

namespace Raven
{
namespace
{
constexpr std::string_view StaticSceneVisibilityDiagnosticPipelineName =
    "Static Scene Visibility Diagnostic Pipeline";

// 診断Materialは毎frame描画されるため、Consoleを埋めないよう最初の1 Drawだけ詳細を出します。
bool s_StaticSceneVisibilityDiagnosticDrawLogged = false;
}

RendererStatistics Renderer::s_Statistics{};
RendererCameraContext Renderer::s_CameraContext{};

// 呼び出し元はApplication::Application()
void Renderer::Init()
{
    RenderCommand::SetAPI(std::make_unique<OpenGLRendererAPI>());
    RenderCommand::Init();
}

void Renderer::BeginFrame()
{
    // CPU ProfilerもApplication frame単位で集計します。
    // Renderer::BeginFrame()は既にApplication::Run()から毎frame先頭で1回だけ呼ばれるため、
    // Renderer統計とCPU統計のframe境界を同じ場所へ揃えられます。
    CPUProfiler::Get().BeginFrame();

    // Statisticsは「直近1 frame」の値として扱います。
    // SceneごとではなくApplication frameの先頭でResetすることで、Scene描画と
    // Debug Overlayを含む、そのframeに発行された全Draw Callを同じ集計へ含めます。
    s_Statistics.Reset();
}

void Renderer::BeginScene(const Camera& camera)
{
    // ========================================================================
    // Camera Context
    // ========================================================================
    // SceneCamera / EditorCameraのどちらで描画しているかはRendererでは区別しません。
    // Camera共通インターフェースから最終View/Projectionだけをコピーし、通常描画と
    // EndScene()内のDebug Passが必ず同じCameraを見るようにします。
    s_CameraContext.View = camera.GetViewMatrix();
    s_CameraContext.Projection = camera.GetProjectionMatrix();
    s_CameraContext.Valid = true;
}

void Renderer::BeginScene()
{
    // Cameraを持たないSandbox等の旧描画経路ではContextを明示的に無効化します。
    // 前回SceneのCameraが残ったままDebug Passへ誤利用されることを防ぎます。
    s_CameraContext.Valid = false;
}

void Renderer::EndScene()
{
    RAVEN_PROFILE_SCOPE("Renderer::DebugOverlay");

    // ========================================================================
    // Debug Overlay Pass
    // ========================================================================
    // 通常のScene描画が完了した後に、Physics / Animationのデバッグ表示を重ねます。
    // Camera依存のPhysics DebugはRenderer Camera Contextを参照するため、Game Viewでは
    // SceneCamera、Scene ViewではEditorCameraへ自動的に追従します。
    ph::PhysicsDebugRenderer::RenderRegistered();
    AnimationDebugOverlayRenderer::RenderRegistered();
}

const RendererCameraContext& Renderer::GetCameraContext()
{
    return s_CameraContext;
}

void Renderer::Shutdown()
{
}

RendererAPI& Renderer::GetAPI()
{
    return RenderCommand::GetAPI();
}

const RendererStatistics& Renderer::GetStatistics()
{
    return s_Statistics;
}

void Renderer::RecordIndexedDraw(uint32_t indexCount)
{
    ++s_Statistics.DrawCalls;
    s_Statistics.IndexCount += indexCount;

    // 現在のRavenのIndexed描画はTriangle Listを前提としているため3 index = 1 triangleです。
    // Line/Point topologyを追加する場合はPrimitiveTopologyを統計APIへ渡す形へ拡張します。
    s_Statistics.TriangleCount += indexCount / 3u;
}

void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray)
{
    shader->Bind();
    vertexArray->Bind();
    RenderCommand::DrawIndexed(vertexArray);
}

void Renderer::DrawIndexed(const Ref<VertexArray>& vertexArray)
{
    RenderCommand::DrawIndexed(vertexArray);
}

void Renderer::Draw(const Ref<Mesh>& mesh, const Ref<Material>& material, const math::Mat4& transform)
{
    if (mesh == nullptr || material == nullptr)
    {
        return;
    }

    const Ref<Pipeline>& pipeline = material->GetPipeline();
    bool logStaticSceneDiagnosticDraw = false;
    if (pipeline != nullptr
        && pipeline->GetSpecification().DebugName != nullptr
        && std::string_view{ pipeline->GetSpecification().DebugName }
            == StaticSceneVisibilityDiagnosticPipelineName
        && s_StaticSceneVisibilityDiagnosticDrawLogged == false)
    {
        logStaticSceneDiagnosticDraw = true;

        const Ref<MeshGeometry>& geometry = mesh->GetGeometry();
        std::cout
            << "[Renderer::Draw] StaticScene診断MaterialがDraw経路へ到達しました\n"
            << "  CameraContext=" << (s_CameraContext.Valid ? "valid" : "invalid") << '\n'
            << "  Pipeline=" << pipeline->GetSpecification().DebugName << '\n'
            << "  VertexArray=" << (mesh->GetVertexArray() != nullptr ? "valid" : "nullptr") << '\n';

        if (geometry != nullptr)
        {
            std::cout
                << "  Vertices=" << geometry->GetVertices().size()
                << " Indices=" << geometry->GetIndices().size()
                << " Triangles=" << geometry->GetIndices().size() / 3u << '\n';
        }
        else
        {
            std::cout << "  Geometry=nullptr\n";
        }
    }

    // ========================================================================
    // Per-draw Camera Uniform
    // ========================================================================
    // Scene側がu_View/u_Projectionを個別に設定すると、Game ViewとScene ViewでCameraの
    // 切り替え責務が各Sceneへ漏れてしまいます。Camera付きBeginScene()で確定したContextを
    // Renderer::Draw()からMaterialへ反映し、通常描画とDebug PassのCameraを統一します。
    //
    // CameraなしBeginScene()を利用する旧Sandbox経路ではValid=falseとなるため、既存の
    // 手動Uniform設定を上書きしません。これにより段階的なRenderer移行も維持できます。
    if (s_CameraContext.Valid)
    {
        material->SetUniform("u_View", s_CameraContext.View);
        material->SetUniform("u_Projection", s_CameraContext.Projection);
    }

    material->SetUniform("u_Model", transform);
    material->Bind(RenderCommand::GetAPI());
    mesh->Draw();

    if (logStaticSceneDiagnosticDraw == true)
    {
        std::cout << "  Mesh::Draw()まで実行しました\n";
        s_StaticSceneVisibilityDiagnosticDrawLogged = true;
    }
}

}