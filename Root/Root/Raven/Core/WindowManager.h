#pragma once
#include "Raven/Core/Window.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"
#include "Raven/Renderer/Buffer/VertexArray.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Raven
{

// Windowの識別子はNative Handleと切り離します。破棄済みWindowのIDを再利用しません。
using WindowID = std::size_t;

class WindowManager
{
public:
    WindowManager() = default;
    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    // Applicationが所有するMain Windowは借用登録し、Managerからは破棄しません。
    WindowID RegisterWindow(Window& window)
    {
        const WindowID id = m_NextID++;
        m_Windows.emplace(id, Entry{ &window, nullptr });
        return id;
    }

    // 将来のEditor補助WindowをManagerが所有するための入口です。
    // Window::Createが失敗した場合は無効ID(0)を返します。
    WindowID CreateWindow(const WindowSpecification& specification = WindowSpecification(),
        Window::EventCallbackFn callback = {})
    {
        WindowSpecification windowSpecification = specification;
        // Main Window等の既存OpenGL Contextと共有してGPU資産を再アップロードしません。
        // Vulkan/DX12のNo-API Windowにはshare引数を渡しません。
        if (windowSpecification.Backend == RHIBackend::OpenGL &&
            windowSpecification.ShareContext == nullptr)
        {
            // unordered_mapの反復順に依存せず、最初に登録した生存Windowを共有元にします。
            // ApplicationのMain Windowは最初に登録されるため通常はMainが選ばれます。
            WindowID oldestID = 0;
            for (const auto& item : m_Windows)
            {
                if (item.second.Handle != nullptr &&
                    item.second.Handle->GetBackend() == RHIBackend::OpenGL &&
                    (oldestID == 0 || item.first < oldestID))
                {
                    oldestID = item.first;
                    windowSpecification.ShareContext = item.second.Handle->GetNativeWindow();
                }
            }
        }
        std::unique_ptr<Window> window = Window::Create(windowSpecification);
        if (window == nullptr || window->GetNativeWindow() == nullptr)
        {
            return 0;
        }

        const WindowID id = m_NextID++;
        Window* rawWindow = window.get();
        m_Windows.emplace(id, Entry{ rawWindow, std::move(window) });
        // 補助WindowのEventはMain WindowのUIContextへ流さず、Windowごとに配送します。
        // Close中のGLFW callbackから直接Windowを破棄するとuse-after-freeになるため遅延します。
        rawWindow->SetEventCallback([this, id, callback = std::move(callback)](Event& event)
            {
                if (static_cast<bool>(callback) == true)
                {
                    callback(event);
                }
                if (event.GetEventType() == EventType::WindowClose)
                {
                    RequestWindowClose(id);
                }
            });
        return id;
    }

    Window* GetWindow(WindowID id)
    {
        const auto it = m_Windows.find(id);
        return it == m_Windows.end() ? nullptr : it->second.Handle;
    }

    const Window* GetWindow(WindowID id) const
    {
        const auto it = m_Windows.find(id);
        return it == m_Windows.end() ? nullptr : it->second.Handle;
    }

    // 描画先のLifecycleはWindow単位で保持し、Native Windowより先に破棄します。
    // 現行のApplication Main WindowのLifecycleはRun()が所有するため、ここへ二重登録しません。
    // Vulkan/DX12のScene Lifecycleは未実装なので、Create失敗時はfalseを返します。
    bool AttachFrameLifecycle(WindowID id)
    {
        const auto it = m_Windows.find(id);
        if (it == m_Windows.end() || it->second.FrameLifecycle != nullptr)
        {
            return false;
        }
        Scope<RHISceneFrameLifecycle> lifecycle =
            RHISceneFrameLifecycle::Create(*it->second.Handle);
        if (lifecycle == nullptr)
        {
            return false;
        }
        it->second.FrameLifecycle = std::move(lifecycle);
        return true;
    }

    RHISceneFrameLifecycle* GetFrameLifecycle(WindowID id)
    {
        const auto it = m_Windows.find(id);
        return it == m_Windows.end() ? nullptr : it->second.FrameLifecycle.get();
    }

    // 補助Windowを一Frame描画します。Main WindowのContextを呼び出し後に復元します。
    // draw内では対象WindowのContextがCurrentです。GPU Resource共有は保証しません。
    // Windowの破棄・登録変更をdraw中に行わないでください。
    bool RenderWindow(WindowID id, WindowID restoreWindowID,
        const std::function<void(Window&)>& draw)
    {
        const auto it = m_Windows.find(id);
        const auto restoreIt = m_Windows.find(restoreWindowID);
        if (it == m_Windows.end() || restoreIt == m_Windows.end() ||
            it->second.FrameLifecycle == nullptr || it->second.CloseRequested == true ||
            static_cast<bool>(draw) == false ||
            id == restoreWindowID)
        {
            return false;
        }

        Window& window = *it->second.Handle;
        Window& restoreWindow = *restoreIt->second.Handle;
        RHISceneFrameLifecycle& frame = *it->second.FrameLifecycle;
        if (window.GetBackend() != RHIBackend::OpenGL ||
            restoreWindow.GetBackend() != RHIBackend::OpenGL ||
            window.GetState() == WindowState::Minimized ||
            window.GetFramebufferWidth() == 0 || window.GetFramebufferHeight() == 0)
        {
            return false;
        }

        if (frame.BeginFrame() != RHIFrameResult::Success)
        {
            restoreWindow.MakeContextCurrent();
            return false;
        }
        // 共有ContextのTexture等は利用できますが、FBO/VAOは共有されません。
        // この経路では補助Windowの既定Framebufferを描画先とします。
        // 独自FBOを使うCallbackは、自分でそのContextに属するFBOをBindしてください。
        if (window.BindDefaultFramebuffer() == false ||
            window.SetFramebufferViewport() == false)
        {
            // FrameがActiveのまま残らないよう、Begin成功後は必ずEnd/Presentします。
            frame.EndFrame();
            frame.Present();
            restoreWindow.MakeContextCurrent();
            return false;
        }
        draw(window);
        const bool ended = frame.EndFrame() == RHIFrameResult::Success;
        const bool presented = ended == true &&
            frame.Present() == RHIFrameResult::Success;
        // 補助WindowのSwapBuffers後、Main WindowのContextへ戻します。
        const bool restored = restoreWindow.MakeContextCurrent();
        return presented == true && restored == true;
    }

    // Renderer用の入口。補助Window専用VAOを取得してから描画Callbackへ渡します。
    // Callbackは取得したVAOをBindしてRenderCommand::DrawIndexed等へ渡せます。
    // VAOをCallback外へ保存する場合はWindow終了前にその参照を解放してください。
    bool RenderWindowWithVertexArray(WindowID id, WindowID restoreWindowID,
        const Ref<VertexArray>& source,
        const std::function<void(Window&, const Ref<VertexArray>&)>& draw)
    {
        if (source == nullptr || static_cast<bool>(draw) == false)
        {
            return false;
        }
        // Windowの描画Frame開始前にVAOを準備し、Frame中のContext切替を避けます。
        Ref<VertexArray> windowVAO =
            GetOrCreateWindowVertexArray(id, restoreWindowID, source);
        if (windowVAO == nullptr)
        {
            return false;
        }
        const bool rendered = RenderWindow(id, restoreWindowID,
            [&draw, &windowVAO](Window& window)
            {
                draw(window, windowVAO);
            });
        // 破棄要求時に外部参照として残らないよう一時参照を明示的に解放します。
        windowVAO.reset();
        return rendered;
    }

    // Rendererは補助Windowの描画Callback内で、このAPIからWindow専用VAOを取得します。
    // 外部に保持するVAO参照はWindowの破棄前に解放してください。
    // WindowごとのVAOをキャッシュします。初回だけ共有Bufferから対象Context用に再構築します。
    // 呼び出し前後のCurrent Contextを維持するため、復元先Windowを明示します。
    // 元VAOのLayout/IndexBufferを変更した場合はInvalidateWindowVertexArraysで再生成してください。
    Ref<VertexArray> GetOrCreateWindowVertexArray(WindowID id, WindowID restoreWindowID,
        const Ref<VertexArray>& source)
    {
        const auto it = m_Windows.find(id);
        const auto restoreIt = m_Windows.find(restoreWindowID);
        if (it == m_Windows.end() || restoreIt == m_Windows.end() ||
            source == nullptr || id == restoreWindowID ||
            it->second.CloseRequested == true ||
            it->second.Handle->GetBackend() != RHIBackend::OpenGL ||
            restoreIt->second.Handle->GetBackend() != RHIBackend::OpenGL)
        {
            return nullptr;
        }

        const auto cached = it->second.VertexArrays.find(source.get());
        if (cached != it->second.VertexArrays.end())
        {
            return cached->second.second;
        }

        if (it->second.Handle->MakeContextCurrent() == false)
        {
            restoreIt->second.Handle->MakeContextCurrent();
            return nullptr;
        }

        Ref<VertexArray> clone = source->CloneForCurrentContext();
        // Contextを切り替えたままにせず、呼び出し側の描画先へ戻します。
        if (restoreIt->second.Handle->MakeContextCurrent() == false)
        {
            // VAO破棄は生成元Contextで行います。
            it->second.Handle->MakeContextCurrent();
            clone.reset();
            restoreIt->second.Handle->MakeContextCurrent();
            return nullptr;
        }
        if (clone != nullptr)
        {
            it->second.VertexArrays.emplace(source.get(), std::make_pair(source, clone));
        }
        return clone;
    }

    // VAOは生成先ContextがCurrentの間に破棄します。Window破棄前に必ず呼びます。
    bool InvalidateWindowVertexArrays(WindowID id, WindowID restoreWindowID)
    {
        const auto it = m_Windows.find(id);
        const auto restoreIt = m_Windows.find(restoreWindowID);
        if (it == m_Windows.end() || restoreIt == m_Windows.end() ||
            id == restoreWindowID || it->second.Handle->GetBackend() != RHIBackend::OpenGL ||
            restoreIt->second.Handle->GetBackend() != RHIBackend::OpenGL)
        {
            return false;
        }
        if (it->second.Handle->MakeContextCurrent() == false)
        {
            restoreIt->second.Handle->MakeContextCurrent();
            return false;
        }
        // 外部参照が残る場合はContext破棄後のglDeleteVertexArraysを避けるため拒否します。
        for (const auto& item : it->second.VertexArrays)
        {
            if (item.second.second.use_count() != 1)
            {
                restoreIt->second.Handle->MakeContextCurrent();
                return false;
            }
        }
        it->second.VertexArrays.clear();
        return restoreIt->second.Handle->MakeContextCurrent();
    }

    // Close通知時点でRendererが保持する補助Context専用VAO/FBO等を解放する入口です。
    // callbackはGLFW event callback内では実行せず、PollEvents終了後に対象Contextで呼びます。
    // 再入による二重解放を避けるため、同じClose要求に対して一度だけ実行します。
    bool SetWindowCloseCleanup(WindowID id, std::function<void(Window&)> cleanup)
    {
        const auto it = m_Windows.find(id);
        if (it == m_Windows.end() || it->second.CloseRequested == true)
        {
            return false;
        }
        it->second.CloseCleanup = std::move(cleanup);
        return true;
    }

    bool RequestWindowClose(WindowID id)
    {
        const auto it = m_Windows.find(id);
        if (it == m_Windows.end())
        {
            return false;
        }
        if (it->second.CloseRequested == false)
        {
            it->second.CloseRequested = true;
            m_PendingClose.push_back(id);
        }
        return true;
    }

    bool IsWindowClosePending(WindowID id) const
    {
        const auto it = m_Windows.find(id);
        return it != m_Windows.end() && it->second.CloseRequested == true;
    }

    bool DetachFrameLifecycle(WindowID id)
    {
        const auto it = m_Windows.find(id);
        if (it == m_Windows.end() || it->second.FrameLifecycle == nullptr)
        {
            return false;
        }
        it->second.FrameLifecycle.reset();
        return true;
    }

    bool UnregisterWindow(WindowID id)
    {
        // 借用Windowは登録だけ解除し、所有Windowはunique_ptrによって破棄します。
        if (m_Windows.find(id) == m_Windows.end())
        {
            return false;
        }
        if (m_PollingEvents == true)
        {
            return RequestWindowClose(id);
        }
        // 明示的な解除もClose Eventと同じ破棄経路を使い、Context復元漏れを防ぎます。
        return TryCloseWindow(id);
    }

    // Application終了時、借用Main Windowより先にManager所有の補助Windowを解放します。
    // 外部VAO参照が残るWindowは破棄せずfalseを返すため、所有側で先に参照を解放してください。
    bool ShutdownOwnedWindows()
    {
        if (m_PollingEvents == true)
        {
            return false;
        }

        // unordered_mapを走査しながらeraseしないよう、所有WindowのIDを先に確定します。
        std::vector<WindowID> ownedIDs;
        for (const auto& item : m_Windows)
        {
            if (item.second.OwnedWindow != nullptr)
            {
                ownedIDs.push_back(item.first);
            }
        }

        bool success = true;
        for (WindowID id : ownedIDs)
        {
            if (TryCloseWindow(id) == false)
            {
                success = false;
            }
        }
        // 既に破棄したWindowのClose要求を次回PollEventsへ持ち越しません。
        std::vector<WindowID> remaining;
        for (WindowID id : m_PendingClose)
        {
            if (m_Windows.find(id) != m_Windows.end())
            {
                remaining.push_back(id);
            }
        }
        m_PendingClose.swap(remaining);
        RestoreSurvivingContext();
        return success;
    }

    std::size_t GetWindowCount() const { return m_Windows.size(); }

    // GLFWのEvent QueueはWindow単位ではなくProcess単位です。
    // 全WindowでPollEventsを呼ぶと同じFrame内で余分にQueueを処理するため、一度だけ呼びます。
    void PollEvents()
    {
        m_PollingEvents = true;
        if (m_Windows.empty() == false)
        {
            Window* window = m_Windows.begin()->second.Handle;
            if (window != nullptr)
            {
                window->PollEvents();
            }
        }

        // GLFWが全callbackを返した後にだけ補助Windowを破棄します。
        m_PollingEvents = false;
        std::vector<WindowID> pendingClose;
        pendingClose.swap(m_PendingClose);
        for (WindowID id : pendingClose)
        {
            if (TryCloseWindow(id) == false && m_Windows.find(id) != m_Windows.end())
            {
                // 外部VAO参照が残る場合は次回のPollEventsで再試行します。
                m_PendingClose.push_back(id);
            }
        }
        // 複数Windowを同時に閉じても、破棄済みContextをCurrentのままにしません。
        RestoreSurvivingContext();
    }

private:
    // unordered_mapの反復順ではなく登録順で復元先を選びます。
    // Main Windowが残っていれば最も小さいIDなので通常はMainへ戻ります。
    bool RestoreSurvivingContext()
    {
        WindowID oldestID = 0;
        Window* candidate = nullptr;
        for (const auto& item : m_Windows)
        {
            if (item.second.Handle != nullptr &&
                item.second.Handle->GetBackend() == RHIBackend::OpenGL &&
                (oldestID == 0 || item.first < oldestID))
            {
                oldestID = item.first;
                candidate = item.second.Handle;
            }
        }
        return candidate != nullptr && candidate->MakeContextCurrent();
    }

    bool TryCloseWindow(WindowID id)
    {
        const auto it = m_Windows.find(id);
        if (it == m_Windows.end())
        {
            return false;
        }

        // CleanupとVAO解放を対象Contextで行い、Window破棄前に生存Contextへ復帰します。
        Window& window = *it->second.Handle;
        if (RunCloseCleanup(id) == false)
        {
            RestoreSurvivingContext();
            return false;
        }
        if (window.GetBackend() == RHIBackend::OpenGL &&
            it->second.VertexArrays.empty() == false)
        {
            if (window.MakeContextCurrent() == false)
            {
                RestoreSurvivingContext();
                return false;
            }
            for (const auto& item : it->second.VertexArrays)
            {
                if (item.second.second.use_count() != 1)
                {
                    RestoreSurvivingContext();
                    return false;
                }
            }
            it->second.VertexArrays.clear();
        }

        // 復元先を破棄対象以外から探します。最後のWindowならGLFW側の破棄に任せます。
        WindowID oldestID = 0;
        Window* restore = nullptr;
        for (const auto& item : m_Windows)
        {
            if (item.first != id && item.second.Handle != nullptr &&
                item.second.Handle->GetBackend() == RHIBackend::OpenGL &&
                (oldestID == 0 || item.first < oldestID))
            {
                oldestID = item.first;
                restore = item.second.Handle;
            }
        }
        if (restore != nullptr && restore->MakeContextCurrent() == false)
        {
            return false;
        }
        m_Windows.erase(it);
        return true;
    }

    bool RunCloseCleanup(WindowID id)
    {
        const auto it = m_Windows.find(id);
        if (it == m_Windows.end())
        {
            return true;
        }
        Entry& entry = it->second;
        if (entry.CloseCleanupDone == true || static_cast<bool>(entry.CloseCleanup) == false)
        {
            return true;
        }
        if (entry.Handle->GetBackend() == RHIBackend::OpenGL &&
            entry.Handle->MakeContextCurrent() == false)
        {
            return false;
        }
        // Cleanup内ではWindowManagerの登録・解除やWindow破棄を行わないでください。
        // 外部参照を解放した後、VAO cacheのuse_countを確認します。
        entry.CloseCleanupDone = true;
        entry.CloseCleanup(*entry.Handle);
        return true;
    }

    struct Entry
    {
        Window* Handle = nullptr;
        std::unique_ptr<Window> OwnedWindow;
        Scope<RHISceneFrameLifecycle> FrameLifecycle;
        std::function<void(Window&)> CloseCleanup;
        bool CloseRequested = false;
        bool CloseCleanupDone = false;
        // sourceのAddressをKeyに使うため、source自体も保持してAddressの再利用を防ぎます。
        std::unordered_map<const VertexArray*, std::pair<Ref<VertexArray>, Ref<VertexArray>>> VertexArrays;
    };

    WindowID m_NextID = 1;
    std::unordered_map<WindowID, Entry> m_Windows;
    std::vector<WindowID> m_PendingClose;
    bool m_PollingEvents = false;
};

} // namespace Raven
