#include "Renderer.h"

#include "RenderCommand.h"
#include "Raven/Animation/Debug/AnimationDebugOverlayRenderer.h"
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

    material->SetUniform("u_Model", transform);
    material->Bind(RenderCommand::GetAPI());
    mesh->Draw();
}

}