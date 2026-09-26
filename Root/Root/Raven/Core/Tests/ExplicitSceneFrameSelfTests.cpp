#include "Raven/Core/Tests/ExplicitSceneFrameSelfTests.h"

#include "Raven/Core/Application.h"
#include "Raven/Assets/TextureAsset.h"
#include "Raven/Renderer/RHI/RHIClearContext.h"
#include "Raven/Renderer/RHI/RHIDevice.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"
#include "Raven/Renderer/RHI/RHISceneMeshRenderer.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/UI/Text/UIFontAtlas.h"

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

namespace Raven::tests
{
namespace
{
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
