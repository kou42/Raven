#pragma once
#include "Raven/Core/Window.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"

#include <cstddef>
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
        std::unique_ptr<Window> window = Window::Create(specification);
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
                    m_PendingClose.push_back(id);
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
            it->second.FrameLifecycle == nullptr || static_cast<bool>(draw) == false ||
            id == restoreWindowID)
        {
            return false;
        }

        Window& window = *it->second.Handle;
        Window& restoreWindow = *restoreIt->second.Handle;
        RHISceneFrameLifecycle& frame = *it->second.FrameLifecycle;
        if (window.GetBackend() != RHIBackend::OpenGL ||
            restoreWindow.GetBackend() != RHIBackend::OpenGL ||
            window.GetState() == WindowState::Minimized)
        {
            return false;
        }

        if (frame.BeginFrame() != RHIFrameResult::Success)
        {
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
            m_PendingClose.push_back(id);
            return true;
        }
        return m_Windows.erase(id) != 0;
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
            const auto it = m_Windows.find(id);
            if (it != m_Windows.end())
            {
                m_Windows.erase(it);
            }
        }
    }

private:
    struct Entry
    {
        Window* Handle = nullptr;
        std::unique_ptr<Window> OwnedWindow;
        Scope<RHISceneFrameLifecycle> FrameLifecycle;
    };

    WindowID m_NextID = 1;
    std::unordered_map<WindowID, Entry> m_Windows;
    std::vector<WindowID> m_PendingClose;
    bool m_PollingEvents = false;
};

} // namespace Raven
