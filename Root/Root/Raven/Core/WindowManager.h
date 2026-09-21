#pragma once
#include "Raven/Core/Window.h"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>

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
    WindowID CreateWindow(const WindowSpecification& specification = WindowSpecification())
    {
        std::unique_ptr<Window> window = Window::Create(specification);
        if (window == nullptr || window->GetNativeWindow() == nullptr)
        {
            return 0;
        }

        const WindowID id = m_NextID++;
        Window* rawWindow = window.get();
        m_Windows.emplace(id, Entry{ rawWindow, std::move(window) });
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

    bool UnregisterWindow(WindowID id)
    {
        // 借用Windowは登録だけ解除し、所有Windowはunique_ptrによって破棄します。
        return m_Windows.erase(id) != 0;
    }

    std::size_t GetWindowCount() const { return m_Windows.size(); }

    // GLFWのEvent QueueはWindow単位ではなくProcess単位です。
    // 全WindowでPollEventsを呼ぶと同じFrame内で余分にQueueを処理するため、一度だけ呼びます。
    void PollEvents()
    {
        if (m_Windows.empty() == false)
        {
            Window* window = m_Windows.begin()->second.Handle;
            if (window != nullptr)
            {
                window->PollEvents();
            }
        }
    }

private:
    struct Entry
    {
        Window* Handle = nullptr;
        std::unique_ptr<Window> OwnedWindow;
    };

    WindowID m_NextID = 1;
    std::unordered_map<WindowID, Entry> m_Windows;
};

} // namespace Raven
