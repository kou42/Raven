#include "Renderer.h"

#include "RenderCommand.h"
#include "Raven/Animation/Debug/AnimationDebugOverlayRenderer.h"
#include "Raven/Core/CPUProfiler.h"
#include "Raven/Renderer/Camera/Camera.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"

#include "Raven/Platform/OpenGL/OpenGLRendererAPI.h"

namespace Raven
{

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
}

}