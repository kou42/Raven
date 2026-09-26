#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Renderer/RHI/RHISceneMeshRenderer.h"

#include <cstdint>
#include <vector>

namespace Raven
{

class Camera;
class Material;
class Mesh;
class RHIDevice;
class RHIGraphicsPipeline;
class RHISceneCommandList;
class RHISceneFrameLifecycle;
class RHITexture;
enum class RHIFrameResult;
struct RHISceneDrawItem;
struct RHIShaderBinary;
struct RHISceneMesh;
class VertexArray;

// ============================================================================
// RendererStatistics
// ============================================================================
// 1 frame中にRendererが実際に発行した描画量を保持します。
// EditorのStatisticsPanelだけでなく、将来Profilerや自動テストからも参照できるよう
// ImGui依存をRendererへ持ち込まず、純粋なEngine統計データとして定義します。
struct RendererStatistics
{
    uint32_t DrawCalls = 0;
    uint32_t IndexCount = 0;
    uint32_t TriangleCount = 0;

    void Reset()
    {
        DrawCalls = 0;
        IndexCount = 0;
        TriangleCount = 0;
    }
};

// ============================================================================
// RendererCameraContext
// ============================================================================
// 現在のScene描画で利用しているCamera行列をRenderer側へ保持する小さなContextです。
// Cameraオブジェクトへの参照を保持せず行列をコピーするため、EditorCamera/SceneCameraの
// 具体型や寿命にRendererが依存しません。
struct RendererCameraContext
{
    math::Mat4 View = math::Mat4::Identity();
    math::Mat4 Projection = math::Mat4::Identity();
    bool Valid = false;
};

class Renderer
{
public:
    static void Init();
    static bool TryInit(RHIBackend backend);
    static void Shutdown();

    static void BeginFrame();

    // Cameraを伴う通常の3D Scene描画入口です。
    // BeginSceneからEndSceneまで、このCameraが通常描画とDebug Passの共通Contextになります。
    static void BeginScene(const Camera& camera);

    // Cameraを必要としない旧Sandbox等の簡易描画用入口です。
    // Camera Contextは無効化されるため、Camera依存のDebug描画は実行されません。
    static void BeginScene();
    static void EndScene();

    // Explicit RuntimeではEndScene時にLegacy描画せず、QueueをPrepareFrameまで保持します。
    static void SetExplicitSceneMode(bool enabled);
    static bool IsExplicitSceneMode();

    static const RendererCameraContext& GetCameraContext();

    static void DrawIndexed(const Ref<VertexArray>& vertexArray);
    static void Draw(const Ref<Mesh>& mesh, const Ref<Material>& material, const math::Mat4& transform);

    // 現在の通常Scene QueueをExplicit RHI用の値Snapshotへ変換します。
    // Buffer生成・更新はFrame開始前に完了している必要があり、ここではGPU操作を行いません。
    // 失敗時はoutMeshesを変更せず、半端なSnapshotを呼び出し側へ残しません。
    static bool BuildRHISceneMeshes(
        const Ref<RHITexture>& defaultTexture,
        std::vector<RHISceneMesh>& outMeshes);

    // 通常RendererのCamera Contextを使い、Explicit RHIへ渡す最終Draw Itemまで構築します。
    static bool BuildRHISceneDrawItems(
        const Ref<RHITexture>& defaultTexture,
        const math::Mat4& clipCorrection,
        std::vector<RHISceneDrawItem>& outItems);

    // 通常MaterialのPipeline stateと標準Mesh Layoutから、Explicit Scene用の
    // Opaque / Transparent Pipelineを同じRender Target向けに生成します。
    static bool CreateRHIScenePipelines(
        RHIDevice& device,
        const Material& material,
        const RHIShaderBinary& vertexShader,
        const RHIShaderBinary& fragmentShader,
        Ref<RHIGraphicsPipeline>& outOpaquePipeline,
        Ref<RHIGraphicsPipeline>& outTransparentPipeline);

    // Legacy Pipeline実体を生成できないExplicit-only起動では、共通Specificationを
    // 直接受け取り、同じPipeline変換規約を利用します。
    static bool CreateRHIScenePipelines(
        RHIDevice& device,
        const PipelineSpecification& source,
        const RHIShaderBinary& vertexShader,
        const RHIShaderBinary& fragmentShader,
        Ref<RHIGraphicsPipeline>& outOpaquePipeline,
        Ref<RHIGraphicsPipeline>& outTransparentPipeline);

    // Queueを閉じ、Texture Binding準備後にExplicit RHIのBegin/Draw/End/Presentを実行します。
    // Debug OverlayはまだLegacy専用のため、この経路には含めません。
    // BeginFrame前に構築・Descriptor準備を済ませる描画Snapshotです。
    struct PreparedRHISceneFrame
    {
        std::vector<RHISceneDrawItem> Items;
        Ref<RHIGraphicsPipeline> OpaquePipeline;
        Ref<RHIGraphicsPipeline> TransparentPipeline;
    };

    static bool PrepareRHISceneFrame(
        RHIDevice& device,
        const Ref<RHIGraphicsPipeline>& opaquePipeline,
        const Ref<RHIGraphicsPipeline>& transparentPipeline,
        const Ref<RHITexture>& defaultTexture,
        const math::Mat4& clipCorrection,
        PreparedRHISceneFrame& outFrame);

    // 呼び出し側がBeginFrameを実行済みのときに使用します。
    static RHIFrameResult DrawPreparedRHISceneFrame(
        RHISceneFrameLifecycle& frame,
        RHISceneCommandList& commands,
        const PreparedRHISceneFrame& preparedFrame);

    static RHIFrameResult DrawRHISceneFrame(
        RHIDevice& device,
        RHISceneFrameLifecycle& frame,
        RHISceneCommandList& commands,
        const Ref<RHIGraphicsPipeline>& opaquePipeline,
        const Ref<RHIGraphicsPipeline>& transparentPipeline,
        const Ref<RHITexture>& defaultTexture,
        const math::Mat4& clipCorrection);

    static const RendererStatistics& GetStatistics();
    static void RecordIndexedDraw(uint32_t indexCount, PrimitiveTopology topology);

private:
    static RendererStatistics s_Statistics;
    static RendererCameraContext s_CameraContext;
};

}