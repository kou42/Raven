#include "Renderer.h"

#include "RenderCommand.h"
#include "Raven/Animation/Debug/AnimationDebugOverlayRenderer.h"
#include "Raven/Core/CPUProfiler.h"
#include "Raven/Renderer/Camera/Camera.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/RHI/RHIDevice.h"
#include "Raven/Renderer/RHI/RHISceneDrawItemBuilder.h"
#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"

#include <algorithm>
#include <limits>
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
    float SortDepth = 0.0f;
};

std::vector<SceneRenderItem> s_OpaqueQueue;
std::vector<SceneRenderItem> s_TransparentQueue;
bool s_SceneQueueActive = false;

float ComputeFallbackSortDepth(const math::Mat4& transform, const math::Mat4& view)
{
    const math::Vec4 worldPosition = transform * math::Vec4{ 0.0f, 0.0f, 0.0f, 1.0f };
    const math::Vec4 viewPosition = view * worldPosition;

    // RavenのView spaceではCamera前方が-Zなので、正のSortDepthへ変換します。
    return -viewPosition.z;
}

float ComputeTransparentSortDepth(
    const Ref<Mesh>& mesh,
    const math::Mat4& transform,
    const math::Mat4& view)
{
    if (mesh == nullptr)
    {
        return ComputeFallbackSortDepth(transform, view);
    }

    const Ref<MeshGeometry>& geometry = mesh->GetGeometry();
    if (geometry == nullptr)
    {
        // Physics Debug等の低レベルMeshはMeshGeometryを持たない場合があります。
        // 通常Scene Queueへ入った場合でも安全にEntity原点へフォールバックします。
        return ComputeFallbackSortDepth(transform, view);
    }

    math::Vec3 localMinimum{};
    math::Vec3 localMaximum{};
    if (geometry->GetLocalBounds(localMinimum, localMaximum) == false)
    {
        return ComputeFallbackSortDepth(transform, view);
    }

    // ========================================================================
    // Local AABB -> World/View space transparent sort key
    // ========================================================================
    // MeshのLocal Bounds 8頂点をWorldへ変換し、さらにView spaceへ移して最奥Depthを求めます。
    // Entity原点だけでなくMeshの大きさ・回転・非一様ScaleをSortへ反映できるため、
    // 水槽の壁のような大きい透明Meshと小さい透明Meshが混在する場合の順序が安定します。
    // Projection/NDCを使わないためOpenGL / DirectX / Vulkan間のClip space差異にも依存しません。
    float farthestDepth = std::numeric_limits<float>::lowest();

    for (int xIndex = 0; xIndex < 2; ++xIndex)
    {
        for (int yIndex = 0; yIndex < 2; ++yIndex)
        {
            for (int zIndex = 0; zIndex < 2; ++zIndex)
            {
                const math::Vec3 localPoint{
                    xIndex == 0 ? localMinimum.x : localMaximum.x,
                    yIndex == 0 ? localMinimum.y : localMaximum.y,
                    zIndex == 0 ? localMinimum.z : localMaximum.z
                };

                const math::Vec4 worldPoint = transform * math::Vec4{ localPoint, 1.0f };
                const math::Vec4 viewPoint = view * worldPoint;
                const float depth = -viewPoint.z;
                farthestDepth = std::max(farthestDepth, depth);
            }
        }
    }

    return farthestDepth;
}

bool BuildRHISceneMeshSnapshot(
    const SceneRenderItem& item,
    const Ref<RHITexture>& defaultTexture,
    RHISceneMesh& outMesh)
{
    if (item.Mesh == nullptr || item.Material == nullptr ||
        item.Mesh->AreRHIResourcesSynchronized() == false ||
        item.Mesh->GetIndexCount() == 0)
    {
        return false;
    }

    RHIMaterialProperties material = item.Material->GetRHIProperties();
    if (material.SurfaceType == MaterialSurfaceType::Masked)
    {
        // Masked描画はalpha cutoff対応Shaderが揃うまでExplicit Scene RHI側で未対応です。
        return false;
    }
    if (material.Texture == nullptr)
    {
        material.Texture = defaultTexture;
    }
    if (material.Texture == nullptr)
    {
        return false;
    }

    RHISceneMesh snapshot{};
    snapshot.VertexBuffer = item.Mesh->GetRHIVertexBuffer();
    snapshot.IndexBuffer = item.Mesh->GetRHIIndexBuffer();
    snapshot.IndexCount = item.Mesh->GetIndexCount();
    snapshot.Material = std::move(material);
    snapshot.Model = RHISceneDrawItemBuilder::ToColumnMajor(item.Transform);

    const Ref<MeshGeometry>& geometry = item.Mesh->GetGeometry();
    math::Vec3 localMinimum{};
    math::Vec3 localMaximum{};
    if (geometry != nullptr &&
        geometry->GetLocalBounds(localMinimum, localMaximum))
    {
        // Transparent sortの代表点には、Entity原点ではなくLocal Bounds中心を使用します。
        const math::Vec3 localCenter = (localMinimum + localMaximum) * 0.5f;
        snapshot.LocalCenter = {
            localCenter.x,
            localCenter.y,
            localCenter.z
        };
    }

    outMesh = std::move(snapshot);
    return true;
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
    item.Material->BindForSurface();
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
    // Alpha Blendは描画順に依存するため、Mesh Boundsの最奥Depthが大きいものから描画します。
    // View spaceまでの計算で完結しProjection/NDCを使わないため、Graphics API差異へ依存しません。
    std::stable_sort(
        s_TransparentQueue.begin(),
        s_TransparentQueue.end(),
        [](const SceneRenderItem& lhs, const SceneRenderItem& rhs)
        {
            return lhs.SortDepth > rhs.SortDepth;
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
    // Graphics Backendの生成と初期state設定はRenderCommand/RHI側へ集約します。
    // Renderer上位層はOpenGL等の具体Backendを直接生成しません。
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

const RendererStatistics& Renderer::GetStatistics()
{
    return s_Statistics;
}

void Renderer::RecordIndexedDraw(uint32_t indexCount, PrimitiveTopology topology)
{
    ++s_Statistics.DrawCalls;
    s_Statistics.IndexCount += indexCount;

    // TriangleCountはTriangle Listだけを対象にします。
    // Physics DebugのLinesや将来のPointsもDrawCalls/IndexCountには含めますが、
    // 三角形数へは加算しないことでStatisticsの意味をTopology間で維持します。
    if (topology == PrimitiveTopology::Triangles)
    {
        s_Statistics.TriangleCount += indexCount / 3u;
    }
}

void Renderer::DrawIndexed(const Ref<VertexArray>& vertexArray)
{
    RenderCommand::DrawIndexed(vertexArray);
}

bool Renderer::BuildRHISceneMeshes(
    const Ref<RHITexture>& defaultTexture,
    std::vector<RHISceneMesh>& outMeshes)
{
    if (s_SceneQueueActive == false || s_CameraContext.Valid == false)
    {
        return false;
    }

    std::vector<RHISceneMesh> snapshots;
    snapshots.reserve(s_OpaqueQueue.size() + s_TransparentQueue.size());

    const auto appendQueue =
        [&snapshots, &defaultTexture](const std::vector<SceneRenderItem>& queue)
        {
            for (const SceneRenderItem& item : queue)
            {
                RHISceneMesh snapshot{};
                if (BuildRHISceneMeshSnapshot(
                    item, defaultTexture, snapshot) == false)
                {
                    return false;
                }
                snapshots.push_back(std::move(snapshot));
            }
            return true;
        };

    // Opaque / Transparentの分類は通常Rendererと共有し、Transparentの最終sortは
    // Camera行列を適用するRHISceneDrawItemBuilderへ委譲します。
    if (appendQueue(s_OpaqueQueue) == false ||
        appendQueue(s_TransparentQueue) == false)
    {
        return false;
    }

    outMeshes = std::move(snapshots);
    return true;
}

bool Renderer::BuildRHISceneDrawItems(
    const Ref<RHITexture>& defaultTexture,
    const math::Mat4& clipCorrection,
    std::vector<RHISceneDrawItem>& outItems)
{
    std::vector<RHISceneMesh> meshes;
    if (BuildRHISceneMeshes(defaultTexture, meshes) == false)
    {
        return false;
    }

    // Cameraは参照保持せず、BeginScene()で確定した行列値だけを利用します。
    std::vector<RHISceneDrawItem> items = RHISceneDrawItemBuilder::Build(
        s_CameraContext.View,
        s_CameraContext.Projection,
        meshes,
        clipCorrection);
    outItems = std::move(items);
    return true;
}

bool Renderer::CreateRHIScenePipelines(
    RHIDevice& device,
    const Material& material,
    const RHIShaderBinary& vertexShader,
    const RHIShaderBinary& fragmentShader,
    Ref<RHIGraphicsPipeline>& outOpaquePipeline,
    Ref<RHIGraphicsPipeline>& outTransparentPipeline)
{
    const Ref<Pipeline>& sourcePipeline = material.GetPipeline();
    if (sourcePipeline == nullptr)
    {
        return false;
    }

    RHIGraphicsPipelineTarget target{};
    if (device.GetGraphicsPipelineTarget(target) == false ||
        target.IsValid() == false)
    {
        return false;
    }

    const PipelineSpecification& source = sourcePipeline->GetSpecification();
    RHIGraphicsPipelineSpecification specification{};
    specification.VertexShader = vertexShader;
    specification.FragmentShader = fragmentShader;

    // Mesh::BuildVertexUploadData()の
    // Position(3) + Color(3) + TexCoord(2) + Normal(3) と一致させます。
    constexpr uint32_t floatSize = sizeof(float);
    specification.VertexBindings = {{ 0, 11u * floatSize }};
    specification.VertexAttributes = {
        { 0, 0, ShaderDataType::Float3, 0 },
        { 1, 0, ShaderDataType::Float3, 3u * floatSize },
        { 2, 0, ShaderDataType::Float2, 6u * floatSize },
        { 3, 0, ShaderDataType::Float3, 8u * floatSize }
    };

    specification.Topology = source.Topology;
    specification.Cull = source.Cull;
    specification.FrontFaceMode = source.FrontFaceMode;
    specification.DepthCompare = source.DepthCompare;
    specification.DepthTest = source.DepthTest;
    specification.ColorFormat = target.ColorFormat;
    specification.DepthFormat = target.DepthFormat;
    specification.SampleCount = target.SampleCount;

    // 通常RendererのSurface契約と同じく、OpaqueはDepthを書き込み、
    // TransparentはDepth Testを維持したままBlendを有効化して書き込みを止めます。
    specification.DepthWrite = true;
    specification.Blend = false;
    specification.DebugName = std::string(source.DebugName) + " RHI Opaque";

    Ref<RHIGraphicsPipeline> opaque =
        device.CreateGraphicsPipeline(specification);
    if (opaque == nullptr)
    {
        return false;
    }

    specification.DepthWrite = false;
    specification.Blend = true;
    specification.DebugName = std::string(source.DebugName) + " RHI Transparent";
    Ref<RHIGraphicsPipeline> transparent =
        device.CreateGraphicsPipeline(specification);
    if (transparent == nullptr)
    {
        return false;
    }

    // 両方揃ってから出力を更新し、呼び出し側に片方だけのPipelineを残しません。
    outOpaquePipeline = std::move(opaque);
    outTransparentPipeline = std::move(transparent);
    return true;
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
            item.SortDepth = ComputeTransparentSortDepth(
                mesh,
                transform,
                s_CameraContext.View);
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
    material->Bind();
    mesh->Draw();
}

}
