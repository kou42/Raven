#include "Raven/Core/Tests/ExplicitSceneFrameSelfTests.h"

#include "Raven/Core/Application.h"
#include "Raven/Renderer/RHI/RHIClearContext.h"

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
}
} // namespace Raven::tests
