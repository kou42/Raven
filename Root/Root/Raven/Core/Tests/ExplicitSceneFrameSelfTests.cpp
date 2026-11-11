#include "Raven/Core/Tests/ExplicitSceneFrameSelfTests.h"

#include "Raven/Core/Application.h"
#include "Raven/Assets/TextureAsset.h"
#include "Raven/Renderer/RHI/RHIClearContext.h"
#include "Raven/Renderer/RHI/RHIDevice.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"
#include "Raven/Renderer/RHI/RHISceneDrawItemBuilder.h"
#include "Raven/Renderer/RHI/RHISceneMeshRenderer.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/UI/Text/UIFontAtlas.h"
#include "Raven/Scene/Debug/EditorBackendComparisonScene.h"
#include "Raven/Scene/SceneCameraSystem.h"

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>
#include <sstream>

namespace Raven::tests
{
namespace
{

// OS Windowを作らず、OpenGL Frame開始の失敗と復帰を再現します。
// 最小化の待機はApplicationの責務であり、描画側の失敗判定を緩めない契約を確認します。
class MockOpenGLWindow final : public Window
{
public:
    bool ContextAvailable = true;
    bool ViewportAvailable = false;
    int ViewportCalls = 0;
    int PresentCalls = 0;
    void OnUpdate() override {}
    void PollEvents() override {}
    void Present() override { ++PresentCalls; }
    bool MakeContextCurrent() override { return ContextAvailable; }
    bool SetFramebufferViewport() override { ++ViewportCalls; return ViewportAvailable; }
    unsigned int GetWidth() const override { return 1280u; }
    unsigned int GetHeight() const override { return 720u; }
    RHIBackend GetBackend() const override { return RHIBackend::OpenGL; }
    WindowState GetState() const override { return WindowState::Normal; }
    void SetTitle(const std::string&) override {}
    void SetSize(unsigned int, unsigned int) override {}
    void SetPosition(int, int) override {}
    void Minimize() override {}
    void Maximize() override {}
    void Restore() override {}
    void SetFullscreen(bool) override {}
    void Show() override {}
    void Hide() override {}
    void Focus() override {}
    void* GetNativeWindow() const override { return nullptr; }
    void* GetPlatformWindowHandle() const override { return nullptr; }
    void SetEventCallback(const EventCallbackFn&) override {}
    void SetVSync(bool) override {}
    bool IsVSync() const override { return true; }
};
enum class Call
{
    Discard,
    Resize
};

// 実GPUを使わずAcquireの成否とDrawの呼び出し順を制御します。
class MockSceneFrameLifecycle final : public RHISceneFrameLifecycle
{
public:
    RHIFrameResult BeginResult = RHIFrameResult::Success;
    RHIFrameResult EndResult = RHIFrameResult::Success;
    RHIFrameResult PresentResult = RHIFrameResult::Success;
    std::vector<int> FinishCalls;
    int BeginCount = 0;
    int EndCount = 0;
    int PresentCount = 0;

    RHIFrameResult BeginFrame() override
    {
        ++BeginCount;
        return BeginResult;
    }
    RHIFrameResult EndFrame() override
    {
        ++EndCount;
        FinishCalls.push_back(1);
        return EndResult;
    }
    RHIFrameResult Present() override
    {
        ++PresentCount;
        FinishCalls.push_back(2);
        return PresentResult;
    }
    bool Resize(uint32_t, uint32_t) override
    {
        return true;
    }
};

class MockGraphicsPipeline final : public RHIGraphicsPipeline
{
public:
    explicit MockGraphicsPipeline(RHIGraphicsPipelineSpecification specification)
        : m_Specification(std::move(specification))
    {
    }

    const RHIGraphicsPipelineSpecification& GetSpecification() const override
    {
        return m_Specification;
    }

private:
    RHIGraphicsPipelineSpecification m_Specification;
};

// native Deviceを生成せず、RendererがBackend別に構築したPipeline入力契約だけを記録します。
class MockPipelineDevice final : public RHIDevice
{
public:
    explicit MockPipelineDevice(RHIBackend backend)
        : m_Backend(backend)
    {
    }

    RHIBackend GetBackend() const override
    {
        return m_Backend;
    }

    Ref<RHIBuffer> CreateBuffer(const RHIBufferSpecification&, const void*) override
    {
        return nullptr;
    }

    Ref<RHIGraphicsPipeline> CreateGraphicsPipeline(
        const RHIGraphicsPipelineSpecification& specification) override
    {
        Specifications.push_back(specification);
        return CreateRef<MockGraphicsPipeline>(specification);
    }

    bool GetGraphicsPipelineTarget(RHIGraphicsPipelineTarget& target) const override
    {
        target.ColorFormat = RHIColorFormat::BGRA8Unorm;
        target.DepthFormat = RHIDepthFormat::D32Float;
        target.SampleCount = 1u;
        return true;
    }

    Ref<RHITexture> CreateTexture(
        const RHITextureSpecification&, const void*, std::size_t) override
    {
        return nullptr;
    }

    std::vector<RHIGraphicsPipelineSpecification> Specifications;

private:
    RHIBackend m_Backend = RHIBackend::None;
};

RHIShaderBinary MakeShaderBinary(RHIShaderBinaryFormat format)
{
    RHIShaderBinary binary{};
    binary.Format = format;
    binary.Code = { 1u };
    return binary;
}
} // namespace

void RunExplicitSceneFrameSelfTests()
{
    // 比較SceneのCPU構成はGPUなしでも生成でき、経過時間やBackend専用Demoに依存しません。
    const PipelineSpecification comparisonPipeline = EditorBackendComparisonScene::GetPipelineSpecification();
    assert(comparisonPipeline.Topology == PrimitiveTopology::Triangles);
    assert(comparisonPipeline.Cull == CullMode::None);
    assert(comparisonPipeline.FrontFaceMode == FrontFace::CounterClockwise);
    assert(comparisonPipeline.DepthTest == true && comparisonPipeline.DepthWrite == true);
    assert(comparisonPipeline.DepthCompare == DepthCompareOperator::LessEqual);
    assert(comparisonPipeline.Blend == false && comparisonPipeline.Shader == nullptr);
    EditorBackendComparisonScene comparison(LegacyMeshResourceCreation::Deferred);
    comparison.OnCreate();
    assert(comparison.IsReady() == true);
    std::size_t entityCount = 0u;
    std::size_t meshCount = 0u;
    std::weak_ptr<Mesh> comparisonMesh;
    std::weak_ptr<Material> comparisonMaterial;
    for (auto [entity, tag] : comparison.View<TagComponent>())
    {
        ++entityCount;
        assert(entity.HasComponent<RigidBodyComponent>() == false);
        if (entity.HasComponent<MeshRendererComponent>() == true)
        {
            ++meshCount;
            const auto& renderer = entity.GetComponent<MeshRendererComponent>();
            assert(renderer.Mesh != nullptr && renderer.Material != nullptr);
            assert(renderer.Mesh->GetVertexArray() == nullptr);
            assert(renderer.Material->HasLegacyPipeline() == false);
            assert(renderer.Material->GetSurfaceType() == MaterialSurfaceType::Opaque);
            assert(renderer.Material->GetRHITexture() == nullptr);
            assert(renderer.Material->GetRHITint() == (std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}));
            comparisonMesh = renderer.Mesh;
            comparisonMaterial = renderer.Material;
        }
    }
    assert(entityCount == 5u && meshCount == 4u);
    Entity comparisonCamera = SceneCameraSystem::FindPrimaryCameraEntity(comparison);
    assert(static_cast<bool>(comparisonCamera) == true);
    std::ostringstream initialManifest;
    comparison.WriteManifest(initialManifest);
    comparison.OnCreate(); // 二重AttachでEntityを増やしません。
    comparison.OnUpdate(100.0f);
    std::ostringstream unchangedManifest;
    comparison.WriteManifest(unchangedManifest);
    assert(initialManifest.str() == unchangedManifest.str());
    WindowResizeEvent comparisonResize(1000u, 1000u);
    comparison.OnEvent(comparisonResize);
    SceneCamera* resizedCamera = SceneCameraSystem::UpdatePrimaryCamera(comparison, 1000.0f, 1000.0f);
    assert(resizedCamera != nullptr && resizedCamera->GetAspectRatio() == 1.0f);
    comparison.OnDestroy();
    assert(comparison.IsReady() == false);
    assert(comparison.IsEntityAlive(comparisonCamera) == false);
    assert(comparisonMesh.expired() == true && comparisonMaterial.expired() == true);
    comparison.OnCreate();
    assert(comparison.IsReady() == true);
    comparison.OnDestroy();

    // 0寸法等でViewportを設定できない時、描画側は依然Fatalを返します。
    // 失敗したBeginはActive Frameを残さず、復帰後に同じLifecycleを再利用できます。
    MockOpenGLWindow glWindow;
    OpenGLSceneFrameLifecycle glFrame(glWindow);
    assert(glFrame.BeginFrame() == RHIFrameResult::FatalError);
    assert(glFrame.EndFrame() == RHIFrameResult::FatalError);
    assert(glFrame.Present() == RHIFrameResult::FatalError);
    assert(glWindow.PresentCalls == 0);
    glWindow.ViewportAvailable = true;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        assert(glFrame.BeginFrame() == RHIFrameResult::Success);
        assert(glFrame.BeginFrame() == RHIFrameResult::FatalError);
        assert(glFrame.EndFrame() == RHIFrameResult::Success);
        assert(glFrame.Present() == RHIFrameResult::Success);
        glWindow.ViewportAvailable = false;
        assert(glFrame.BeginFrame() == RHIFrameResult::FatalError);
        glWindow.ViewportAvailable = true;
    }
    assert(glWindow.PresentCalls == 3);
    const int viewportCalls = glWindow.ViewportCalls;
    glWindow.ContextAvailable = false;
    assert(glFrame.BeginFrame() == RHIFrameResult::FatalError);
    assert(glWindow.ViewportCalls == viewportCalls);

    // Picking用EntityIndexはScene QueueからCamera別Snapshotへ値でコピーします。
    // GenerationはReadback後にSceneの正規データから再取得するためGPU Snapshotへ含めません。
    RHISceneMesh pickingMesh{};
    pickingMesh.EntityID = 42u;
    const std::vector<RHISceneDrawItem> pickingItems =
        RHISceneDrawItemBuilder::Build(
            math::Mat4::Identity(), math::Mat4::Identity(),
            std::vector<RHISceneMesh>{ pickingMesh }, math::Mat4::Identity());
    assert(pickingItems.size() == 1u);
    assert(pickingItems[0].EntityID == 42u);

    // Vulkan GLSLはPosition/Color/UVの3属性、現行DXILは未使用NORMALを含む4属性です。
    // 共通Mesh strideは維持しつつ、Pipeline入力だけをShader Signatureへ一致させます。
    PipelineSpecification sourcePipeline{};
    MockPipelineDevice vulkanDevice(RHIBackend::Vulkan);
    Ref<RHIGraphicsPipeline> opaque;
    Ref<RHIGraphicsPipeline> transparent;
    const RHIShaderBinary spirv = MakeShaderBinary(RHIShaderBinaryFormat::SPIRV);
    assert(Renderer::CreateRHIScenePipelines(vulkanDevice, sourcePipeline,
        spirv, spirv, opaque, transparent) == true);
    assert(vulkanDevice.Specifications.size() == 2u);
    assert(vulkanDevice.Specifications[0].VertexAttributes.size() == 3u);
    assert(vulkanDevice.Specifications[1].VertexAttributes.size() == 3u);

    MockPipelineDevice dx12Device(RHIBackend::DirectX12);
    const RHIShaderBinary dxil = MakeShaderBinary(RHIShaderBinaryFormat::DXIL);
    assert(Renderer::CreateRHIScenePipelines(dx12Device, sourcePipeline,
        dxil, dxil, opaque, transparent) == true);
    assert(dx12Device.Specifications.size() == 2u);
    assert(dx12Device.Specifications[0].VertexAttributes.size() == 4u);
    assert(dx12Device.Specifications[0].VertexAttributes[3].Location == 3u);

    Ref<RHIGraphicsPipeline> debugLine;
    assert(Renderer::CreateRHIDebugLinePipeline(
        dx12Device, dxil, dxil, debugLine) == true);
    assert(dx12Device.Specifications.size() == 3u);
    assert(dx12Device.Specifications[2].VertexAttributes.size() == 4u);
    assert(dx12Device.Specifications[2].VertexAttributes[3].Location == 3u);

    Ref<RHIGraphicsPipeline> pickingPipeline;
    assert(Renderer::CreateRHIEntityPickingPipeline(
        dx12Device, sourcePipeline, dxil, dxil, pickingPipeline) == true);
    assert(dx12Device.Specifications.size() == 4u);
    const RHIGraphicsPipelineSpecification& pickingSpecification =
        dx12Device.Specifications[3];
    assert(pickingSpecification.ColorFormat == RHIColorFormat::R32Sint);
    assert(pickingSpecification.Cull == CullMode::None);
    assert(pickingSpecification.DepthCompare == DepthCompareOperator::LessEqual);
    assert(pickingSpecification.DepthTest == true);
    assert(pickingSpecification.DepthWrite == true);
    assert(pickingSpecification.Blend == false);

    Ref<RHIGraphicsPipeline> outlinePipeline;
    assert(Renderer::CreateRHISelectionOutlinePipeline(
        dx12Device, *opaque, dxil, outlinePipeline) == true);
    const RHIGraphicsPipelineSpecification& outline = outlinePipeline->GetSpecification();
    assert(outline.ColorFormat == opaque->GetSpecification().ColorFormat);
    assert(outline.DepthFormat == opaque->GetSpecification().DepthFormat);
    assert(outline.VertexAttributes.size() == 4u);
    assert(outline.Cull == CullMode::Front);
    assert(outline.FrontFaceMode == FrontFace::CounterClockwise);
    assert(outline.DepthCompare == DepthCompareOperator::LessEqual);
    assert(outline.DepthTest == true && outline.DepthWrite == false && outline.Blend == false);

    // TitleのようにCameraを登録しないFrameでも、古いMeshを捨ててUI描画へ進めます。
    Renderer::BeginScene();
    Renderer::PreparedRHISceneFrame uiOnlyFrame;
    uiOnlyFrame.Items.emplace_back();
    const auto clipCorrection = RHISceneDrawItemBuilder::VulkanClipCorrection();
    assert(Renderer::PrepareRHISceneFrame(dx12Device, opaque, transparent,
        nullptr, clipCorrection, uiOnlyFrame) == true);
    assert(uiOnlyFrame.Items.empty() == true);
    assert(uiOnlyFrame.OpaquePipeline == opaque && uiOnlyFrame.TransparentPipeline == transparent);
    assert(Renderer::PrepareRHISceneFrame(dx12Device, nullptr, transparent,
        nullptr, clipCorrection, uiOnlyFrame) == false);
    assert(uiOnlyFrame.OpaquePipeline == nullptr);

    // Explicit BackendのFont AtlasはLegacy Textureを作らず、Pixel Assetを描画準備時にUploadします。
    TextureAssetPixelData atlasPixels{};
    atlasPixels.Width = 2u;
    atlasPixels.Height = 2u;
    atlasPixels.Format = TextureFormat::RGBA8;
    atlasPixels.GenerateMips = false;
    atlasPixels.Pixels.resize(16u, std::byte{ 0xff });
    Ref<TextureAsset> pixelOnlyAsset = CreateRef<TextureAsset>(
        "explicit-font-atlas-test", Ref<Texture>{}, std::move(atlasPixels));
    assert(pixelOnlyAsset->IsValid() == true);
    assert(pixelOnlyAsset->GetTexture() == nullptr);
    assert(pixelOnlyAsset->HasPixelData() == true);
    UIFontAtlas fontAtlas;
    assert(fontAtlas.Initialize(pixelOnlyAsset, 2u, 2u) == true);
    assert(fontAtlas.GetTexture() == pixelOnlyAsset);

    using CallLog = std::vector<Call>;
    CallLog calls;
    uint32_t resizedWidth = 0u;
    uint32_t resizedHeight = 0u;
    bool forceResize = false;
    bool resizeSucceeds = true;

    Application::ExplicitSceneCallbacks callbacks;
    callbacks.DiscardPrepared = [&calls]()
    {
        calls.push_back(Call::Discard);
    };
    callbacks.Resize = [&calls, &resizedWidth, &resizedHeight,
        &forceResize, &resizeSucceeds](uint32_t width, uint32_t height, bool force)
    {
        calls.push_back(Call::Resize);
        resizedWidth = width;
        resizedHeight = height;
        forceResize = force;
        return resizeSucceeds;
    };

    // 成功時はSnapshotを保持したまま次のFrameへ進み、Resizeを呼びません。
    assert(Application::HandleExplicitSceneFrameResult(
        RHIFrameResult::Success, 1280u, 720u, callbacks) == true);
    assert(calls.empty() == true);

    // Surface変更では寸法が不変でもDiscard -> 強制Resizeの順序を守ります。
    assert(Application::HandleExplicitSceneFrameResult(
        RHIFrameResult::ResizeRequired, 1280u, 720u, callbacks) == true);
    assert(calls.size() == 2u);
    assert(calls[0] == Call::Discard);
    assert(calls[1] == Call::Resize);
    assert(resizedWidth == 1280u);
    assert(resizedHeight == 720u);
    assert(forceResize == true);

    // Resizeに失敗してもSnapshotを破棄した状態でRunnerへ失敗を返します。
    calls.clear();
    resizeSucceeds = false;
    assert(Application::HandleExplicitSceneFrameResult(
        RHIFrameResult::ResizeRequired, 1920u, 1080u, callbacks) == false);
    assert(calls.size() == 2u);
    assert(calls[0] == Call::Discard);
    assert(calls[1] == Call::Resize);
    assert(resizedWidth == 1920u);
    assert(resizedHeight == 1080u);

    // FatalErrorではResizeせずSnapshotだけを破棄します。
    calls.clear();
    assert(Application::HandleExplicitSceneFrameResult(
        RHIFrameResult::FatalError, 1920u, 1080u, callbacks) == false);
    assert(calls.size() == 1u);
    assert(calls[0] == Call::Discard);

    // Resize Callbackが未設定でも、先にSnapshotを破棄して安全に失敗します。
    calls.clear();
    callbacks.Resize = nullptr;
    assert(Application::HandleExplicitSceneFrameResult(
        RHIFrameResult::ResizeRequired, 1920u, 1080u, callbacks) == false);
    assert(calls.size() == 1u);
    assert(calls[0] == Call::Discard);

    // Discard Callbackがない場合もFatalErrorは継続扱いにしません。
    calls.clear();
    callbacks.DiscardPrepared = nullptr;
    assert(Application::HandleExplicitSceneFrameResult(
        RHIFrameResult::FatalError, 1920u, 1080u, callbacks) == false);
    assert(calls.empty() == true);

    // Window寸法変更時のResize失敗でも、Frameを開始せずSnapshotを破棄します。
    calls.clear();
    callbacks.DiscardPrepared = [&calls]()
    {
        calls.push_back(Call::Discard);
    };
    assert(Application::HandleExplicitSceneResizeResult(true, callbacks) == true);
    assert(calls.empty() == true);
    assert(Application::HandleExplicitSceneResizeResult(false, callbacks) == false);
    assert(calls.size() == 1u && calls[0] == Call::Discard);

    // Callback未設定でもResize失敗は成功扱いにしません。
    calls.clear();
    callbacks.DiscardPrepared = nullptr;
    assert(Application::HandleExplicitSceneResizeResult(false, callbacks) == false);
    assert(calls.empty() == true);

    // PrepareはBeginFrameより前に実行し、Acquire失敗時はDrawへ進めません。
    MockSceneFrameLifecycle frame;
    std::vector<int> frameCalls;
    const auto prepare = [&frameCalls]()
    {
        frameCalls.push_back(1);
        return true;
    };
    const auto draw = [&frameCalls]()
    {
        frameCalls.push_back(3);
        return RHIFrameResult::Success;
    };
    assert(Application::ExecuteExplicitSceneFrame(frame, prepare, draw) ==
        RHIFrameResult::Success);
    assert(frame.BeginCount == 1);
    assert(frameCalls.size() == 2u);
    assert(frameCalls[0] == 1 && frameCalls[1] == 3);
    // Drawの内部でEndFrame/Presentを行うため、MockのDrawはそれらを呼びません。
    assert(frame.EndCount == 0 && frame.PresentCount == 0);

    frameCalls.clear();
    frame.BeginResult = RHIFrameResult::ResizeRequired;
    assert(Application::ExecuteExplicitSceneFrame(frame, prepare, draw) ==
        RHIFrameResult::ResizeRequired);
    assert(frame.BeginCount == 2);
    assert(frameCalls.size() == 1u && frameCalls[0] == 1);

    frameCalls.clear();
    frame.BeginResult = RHIFrameResult::FatalError;
    assert(Application::ExecuteExplicitSceneFrame(frame, prepare, draw) ==
        RHIFrameResult::FatalError);
    assert(frame.BeginCount == 3);
    assert(frameCalls.size() == 1u && frameCalls[0] == 1);

    // Prepare失敗ではAcquire自体を開始しません。
    frameCalls.clear();
    const auto failedPrepare = [&frameCalls]()
    {
        frameCalls.push_back(1);
        return false;
    };
    assert(Application::ExecuteExplicitSceneFrame(frame, failedPrepare, draw) ==
        RHIFrameResult::FatalError);
    assert(frame.BeginCount == 3);
    assert(frameCalls.size() == 1u && frameCalls[0] == 1);

    // DrawのResizeRequired/FatalErrorは加工せずRunnerの後始末へ伝えます。
    frame.BeginResult = RHIFrameResult::Success;
    const auto resizeDraw = []() { return RHIFrameResult::ResizeRequired; };
    const auto failedDraw = []() { return RHIFrameResult::FatalError; };
    assert(Application::ExecuteExplicitSceneFrame(frame, prepare, resizeDraw) ==
        RHIFrameResult::ResizeRequired);
    assert(Application::ExecuteExplicitSceneFrame(frame, prepare, failedDraw) ==
        RHIFrameResult::FatalError);
    assert(frame.BeginCount == 5);

    // 必須Callback未設定はGPU Frameを開始せず失敗します。
    const std::function<bool()> missingPrepare;
    const std::function<RHIFrameResult()> missingDraw;
    assert(Application::ExecuteExplicitSceneFrame(frame, missingPrepare, draw) ==
        RHIFrameResult::FatalError);
    assert(Application::ExecuteExplicitSceneFrame(frame, prepare, missingDraw) ==
        RHIFrameResult::FatalError);
    assert(frame.BeginCount == 5);

    // Submit成功時だけPresentへ進み、両結果を加工せず返します。
    frame.FinishCalls.clear();
    assert(RHISceneMeshRenderer::FinishActiveFrame(frame) ==
        RHIFrameResult::Success);
    assert(frame.FinishCalls.size() == 2u);
    assert(frame.FinishCalls[0] == 1 && frame.FinishCalls[1] == 2);
    assert(frame.EndCount == 1 && frame.PresentCount == 1);

    // Submit失敗・ResizeRequiredではPresentせず、Context終了/Resize判断へ渡します。
    frame.FinishCalls.clear();
    frame.EndResult = RHIFrameResult::FatalError;
    assert(RHISceneMeshRenderer::FinishActiveFrame(frame) ==
        RHIFrameResult::FatalError);
    assert(frame.FinishCalls.size() == 1u && frame.FinishCalls[0] == 1);
    assert(frame.PresentCount == 1);

    frame.FinishCalls.clear();
    frame.EndResult = RHIFrameResult::ResizeRequired;
    assert(RHISceneMeshRenderer::FinishActiveFrame(frame) ==
        RHIFrameResult::ResizeRequired);
    assert(frame.FinishCalls.size() == 1u && frame.FinishCalls[0] == 1);
    assert(frame.PresentCount == 1);

    // Present失敗・ResizeRequiredはSubmit済みFrameの結果としてRunnerへ伝えます。
    frame.EndResult = RHIFrameResult::Success;
    frame.FinishCalls.clear();
    frame.PresentResult = RHIFrameResult::FatalError;
    assert(RHISceneMeshRenderer::FinishActiveFrame(frame) ==
        RHIFrameResult::FatalError);
    assert(frame.FinishCalls.size() == 2u);
    assert(frame.FinishCalls[0] == 1 && frame.FinishCalls[1] == 2);

    frame.FinishCalls.clear();
    frame.PresentResult = RHIFrameResult::ResizeRequired;
    assert(RHISceneMeshRenderer::FinishActiveFrame(frame) ==
        RHIFrameResult::ResizeRequired);
    assert(frame.FinishCalls.size() == 2u);
    assert(frame.FinishCalls[0] == 1 && frame.FinishCalls[1] == 2);
}
} // namespace Raven::tests
