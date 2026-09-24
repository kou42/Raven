#include "Raven/Core/Tests/ExplicitSceneFrameSelfTests.h"

#include "Raven/Core/Application.h"
#include "Raven/Renderer/RHI/RHIClearContext.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"

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
        return RHIFrameResult::Success;
    }
    RHIFrameResult Present() override
    {
        ++PresentCount;
        return RHIFrameResult::Success;
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
}
} // namespace Raven::tests
