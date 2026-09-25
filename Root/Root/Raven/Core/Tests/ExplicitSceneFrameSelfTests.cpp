#include "Raven/Core/Tests/ExplicitSceneFrameSelfTests.h"

#include "Raven/Core/Application.h"
#include "Raven/Renderer/RHI/RHIClearContext.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"
#include "Raven/Renderer/RHI/RHISceneMeshRenderer.h"

#include <cassert>
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
} // namespace

void RunExplicitSceneFrameSelfTests()
{
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
