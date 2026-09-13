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

#include <algorithm>
#include <vector>

namespace Raven
{
namespace
{
struct SceneRenderItem
{
    Ref<Mesh> Mesh;
    Ref<Material> Material;
    math::Mat4 Transform = math::Mat4::Identity();
    float CameraDistanceSq = 0.0f;
};

std::vector<SceneRenderItem> s_OpaqueQueue;
std::vector<SceneRenderItem> s_TransparentQueue;
bool s_SceneQueueActive = false;

math::Vec3 ExtractCameraPosition(const math::Mat4& view)
{
    // Raven::Mat4::LookAt()は各行にRight / Up / -Forwardを保持します。
    // Graphics APIのNDC規約には依存せず、Engine共通のView行列だけからWorld位置を復元します。
    const math::Vec3 cameraRight{ view[0][0], view[0][1], view[0][2] };
    const math::Vec3 cameraUp{ view[1][0], view[1][1], view[1][2] };
    const math::Vec3 cameraForward{ -view[2][0], -view[2][1], -view[2][2] };

    return
        cameraRight * (-view[0][3])
        + cameraUp * (-view[1][3])
        + cameraForward * view[2][3];
}

float ComputeCameraDistanceSq(const math::Mat4& transform, const math::Vec3& cameraPosition)
{
    // 現段階ではMesh BoundsがRenderer共通情報になっていないため、Entity原点をSort中心にします。
    // 後でWorld-space BoundsをRenderItemへ追加すれば、このQueue構造を変えずに精度を上げられます。
    const math::Vec3 worldPosition{
        transform[0][3],
        transform[1][3],
        transform[2][3]
    };
    const math::Vec3 delta = worldPosition - cameraPosition;
    return delta.LengthSq();
}

void DrawSceneItem(const SceneRenderItem& item, const RendererCameraContext& cameraContext)
{
    if (item.Mesh == nullptr || item.Material == nullptr)
    {
        return;
    }

    item.Material->SetUniform("u_View", cameraContext.View);
    item.Material->SetUniform("u_Projection", cameraContext.Projection);
    item.Material->SetUniform("u_Model", item.Transform);

    // Scene PassではSurface分類から解決したPipelineを使います。
    // Debug Overlay等の即時描画はMaterial::Bind()のままなので、特殊なDepth/Blend stateを壊しません。
    item.Material->BindForSurface(RenderCommand::GetAPI());
    item.Mesh->Draw();
}

void FlushSceneRenderQueues(const RendererCameraContext& cameraContext)
{
    // ========================================================================
    // Opaque Pass
    // ========================================================================
    // OpaqueはDepthWrite=true / Blend=falseのSurface Pipelineで先に描画し、
    // Transparent Passが参照するDepth Bufferをここで完成させます。
    for (const SceneRenderItem& item : s_OpaqueQueue)
    {
        DrawSceneItem(item, cameraContext);
    }

    // ========================================================================
    // Transparent Pass
    // ========================================================================
    // Alpha Blendは描画順に依存するため、Cameraから遠いものを先に描画します。
    // Sort値はWorld-space距離なのでOpenGL / DirectX / VulkanのNDC差異には依存しません。
    std::stable_sort(
        s_TransparentQueue.begin(),
        s_TransparentQueue.end(),
        [](const SceneRenderItem& lhs, const SceneRenderItem& rhs)
        {
            return lhs.CameraDistanceSq > rhs.CameraDistanceSq;
        });

    for (const SceneRenderItem& item : s_TransparentQueue)
    {
        DrawSceneItem(item, cameraContext);
    }

    s_OpaqueQueue.clear();
    s_TransparentQueue.clear();
}
} // namespace

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

    // Camera付き3D SceneだけをQueue描画へ切り替えます。
    // Cameraなしの旧Sandbox経路やEndScene後のLayer描画は従来どおり即時描画です。
    s_OpaqueQueue.clear();
    s_TransparentQueue.clear();
    s_SceneQueueActive = true;
}

void Renderer::BeginScene()
{
    // Cameraを持たないSandbox等の旧描画経路ではContextを明示的に無効化します。
    // 前回SceneのCameraが残ったままDebug Passへ誤利用されることを防ぎます。
    s_CameraContext.Valid = false;
    s_SceneQueueActive = false;
    s_OpaqueQueue.clear();
    s_TransparentQueue.clear();
}

void Renderer::EndScene()
{
    // Flush中のDrawが再びQueueへ入らないよう、先に受付を閉じます。
    s_SceneQueueActive = false;

    if (s_CameraContext.Valid)
    {
        RAVEN_PROFILE_SCOPE("Renderer::ScenePasses");
        FlushSceneRenderQueues(s_CameraContext);
    }

    {
        RAVEN_PROFILE_SCOPE("Renderer::DebugOverlay");

        // ====================================================================
        // Debug Overlay Pass
        // ====================================================================
        // 通常のOpaque/Transparent描画が完了した後にPhysics / Animation表示を重ねます。
        // Debug Rendererは独自Pipeline stateを持つためScene Surface Passへ分類しません。
        ph::PhysicsDebugRenderer::RenderRegistered();
        AnimationDebugOverlayRenderer::RenderRegistered();
    }
}

const RendererCameraContext& Renderer::GetCameraContext()
{
    return s_CameraContext;
}

void Renderer::Shutdown()
{
    s_OpaqueQueue.clear();
    s_TransparentQueue.clear();
    s_SceneQueueActive = false;
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

    if (s_SceneQueueActive && s_CameraContext.Valid)
    {
        SceneRenderItem item{};
        item.Mesh = mesh;
        item.Material = material;
        item.Transform = transform;

        if (material->GetSurfaceType() == MaterialSurfaceType::Transparent)
        {
            const math::Vec3 cameraPosition = ExtractCameraPosition(s_CameraContext.View);
            item.CameraDistanceSq = ComputeCameraDistanceSq(transform, cameraPosition);
            s_TransparentQueue.push_back(std::move(item));
        }
        else
        {
            s_OpaqueQueue.push_back(std::move(item));
        }
        return;
    }

    // ========================================================================
    // Immediate Draw compatibility path
    // ========================================================================
    // CameraなしSandbox、Debug/Layer等は既存どおりMaterial自身のPipelineを尊重して即時描画します。
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
