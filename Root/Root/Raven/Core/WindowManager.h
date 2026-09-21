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
        it->second.VertexArrays.clear();
        return restoreIt->second.Handle->MakeContextCurrent();
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
        // VAOをWindow破棄後に解放しないよう、Contextが生存している間に解放します。
        const auto it = m_Windows.find(id);
        if (it->second.Handle->GetBackend() == RHIBackend::OpenGL &&
            it->second.VertexArrays.empty() == false)
        {
            if (it->second.Handle->MakeContextCurrent() == false)
            {
                return false;
            }
            it->second.VertexArrays.clear();
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
                // GLFW callbackを抜けた後、VAOを対象Contextで先に解放します。
                if (it->second.Handle->GetBackend() == RHIBackend::OpenGL &&
                    it->second.VertexArrays.empty() == false)
                {
                    if (it->second.Handle->MakeContextCurrent() == false)
                    {
                        continue;
                    }
                    it->second.VertexArrays.clear();
                }
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
        // sourceのAddressをKeyに使うため、source自体も保持してAddressの再利用を防ぎます。
        std::unordered_map<const VertexArray*, std::pair<Ref<VertexArray>, Ref<VertexArray>>> VertexArrays;
    };

    WindowID m_NextID = 1;
    std::unordered_map<WindowID, Entry> m_Windows;
    std::vector<WindowID> m_PendingClose;
    bool m_PollingEvents = false;
};

} // namespace Raven
