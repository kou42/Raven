#pragma once
#include "Raven/Core/Window.h"
#include "Raven/Core/WindowManager.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"

#include "Raven/Scene/Scene.h"
#include "Raven/Renderer/RHI/IExplicitSceneRuntime.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Core/Event.h"
#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Immediate/UIImmediateContext.h"

#if defined(_DEBUG)
#include "Raven/UI/Debug/UITreeMutationValidation.h"
#endif

#include <functional>
#include <memory>
#include <unordered_map>
#include <iostream>

namespace Raven
{

class ImGuiLayer;
class RHISceneFrameLifecycle;
enum class RHIFrameResult;
class UIDockSpace;
class UIWindow;

struct ApplicationSpecification
{
    WindowProps WindowProperties{};
    bool EnableRavenUI = true;
    bool EnableDearImGui = true;
    // 既存Editor UIへ重ねるため、Physics Debug Panelは明示的に有効化します。
    bool EnablePhysicsDebugImmediatePanel = false;
    // 文字表示用Atlasは呼び出し側がGPU Context有効時に生成・共有します。
    Ref<UIFontAtlas> PhysicsDebugImmediateFont;
};

class Application
{
public:
    Application();
    explicit Application(const ApplicationSpecification& specification);
    ~Application();

    void Run();

    // Explicit Sceneの準備→Acquire→描画をApplication側の共通Frame進行へ集約します。
    // Context/Runtimeは呼び出し元が所有し、ResizeRequiredは呼び出し元が処理します。
    static RHIFrameResult ExecuteExplicitSceneFrame(
        RHISceneFrameLifecycle& frame,
        const std::function<bool()>& prepare,
        const std::function<RHIFrameResult()>& drawPrepared);

    // Explicit Backendの独立Scene用Loopです。WindowとRuntimeは呼び出し元が所有します。
    // onSceneはRenderer Queueへ描画要求を積み、resizeはSwapChainとCameraを同期します。
    struct ExplicitSceneCallbacks
    {
        std::function<void()> OnScene;
        // Scene/Material/Meshの所有参照をDevice破棄前に解放します。
        std::function<void()> OnBeforeShutdown;
        std::function<bool(uint32_t, uint32_t, bool)> Resize;
        std::function<bool()> Prepare;
        std::function<RHIFrameResult()> DrawPrepared;
        // Acquire失敗時など、準備済みFrameの参照をResize/Shutdown前に解放します。
        std::function<void()> DiscardPrepared;
    };
    static int RunExplicitScene(Window& window, RHISceneFrameLifecycle& frame,
        const ExplicitSceneCallbacks& callbacks);

    // Explicit SceneのWindow/RuntimeをApplicationの実行境界へ移譲します。
    // Callbackは移譲後も有効なRuntime実体を参照する必要があります（Scope変数は参照しません）。
    // GPU ResourceをWindowより先に破棄し、終了順序をBackend間で統一します。
    static int RunOwnedExplicitScene(Scope<Window> window,
        Scope<IExplicitSceneRuntime> runtime, const ExplicitSceneCallbacks& callbacks)
    {
        if (window == nullptr || runtime == nullptr)
        {
            return 1;
        }
        RHISceneFrameLifecycle* frame = runtime->GetFrameLifecycle();
        if (frame == nullptr)
        {
            if (callbacks.OnBeforeShutdown != nullptr)
            {
                callbacks.OnBeforeShutdown();
            }
            Renderer::Shutdown();
            runtime->Shutdown();
            return 1;
        }
        const int exitCode = RunExplicitScene(*window, *frame, callbacks);
        // Frameの保持参照を最初に解放し、SceneとRendererのGPU Resourceを
        // RuntimeのDevice破棄前に解放します。
        if (callbacks.DiscardPrepared != nullptr)
        {
            callbacks.DiscardPrepared();
        }
        if (callbacks.OnBeforeShutdown != nullptr)
        {
            callbacks.OnBeforeShutdown();
        }
        Renderer::Shutdown();
        runtime->Shutdown();
        runtime.reset();
        window.reset();
        return exitCode;
    }

    // Scene固有の処理だけを呼び出し側から受け取り、Runtime操作のCallback配線を共通化します。
    // Hookはこの呼び出しが返るまで有効である必要があります。
    struct ExplicitSceneHooks
    {
        std::function<void()> OnScene;
        std::function<void()> OnBeforeShutdown;
        std::function<void(uint32_t, uint32_t)> OnResizeCamera;
    };

    static int RunOwnedExplicitScene(Scope<Window> window,
        Scope<IExplicitSceneRuntime> runtime, const ExplicitSceneHooks& hooks)
    {
        if (window == nullptr || runtime == nullptr || runtime->IsInitialized() == false ||
            hooks.OnScene == nullptr)
        {
            return 1;
        }

        IExplicitSceneRuntime* runtimeHandle = runtime.get();
        uint32_t resizedWidth = runtimeHandle->GetWidth();
        uint32_t resizedHeight = runtimeHandle->GetHeight();
        ExplicitSceneCallbacks callbacks;
        callbacks.OnScene = hooks.OnScene;
        callbacks.OnBeforeShutdown = hooks.OnBeforeShutdown;
        callbacks.Prepare = [runtimeHandle]() { return runtimeHandle->PrepareFrame(); };
        callbacks.DrawPrepared = [runtimeHandle]() { return runtimeHandle->DrawPreparedFrame(); };
        callbacks.DiscardPrepared = [runtimeHandle]() { runtimeHandle->DiscardPreparedFrame(); };
        callbacks.Resize = [runtimeHandle, resizedWidth, resizedHeight,
            onResizeCamera = hooks.OnResizeCamera](uint32_t width, uint32_t height, bool force) mutable
        {
            // DX12のGetWidth/GetHeightはWindowの現在値を返すため、最後に成功した
            // SwapChainサイズを別途保持し、Window通知による先行更新を見逃しません。
            if (force == false && resizedWidth == width && resizedHeight == height)
            {
                return true;
            }
            if (runtimeHandle->Resize(width, height) == false)
            {
                return false;
            }
            resizedWidth = width;
            resizedHeight = height;
            if (onResizeCamera != nullptr)
            {
                onResizeCamera(width, height);
            }
            return true;
        };
        return RunOwnedExplicitScene(std::move(window), std::move(runtime), callbacks);
    }

    void OnEvent(Event& event);

    void PushLayer(Layer* layer);
    void PushLayer(Scope<Layer> layer);

    void SetScene(Scope<Scene> scene);

    // EditorはApplicationの所有物を借用して表示・操作します。
    // 所有権を渡さず参照だけ公開することで、Scene/Windowの寿命管理は引き続きApplicationへ集約します。
    Scene* GetScene() { return m_scene.get(); }
    const Scene* GetScene() const { return m_scene.get(); }
    Window& GetWindow() { return *m_Window; }
    const Window& GetWindow() const { return *m_Window; }
    WindowManager& GetWindowManager() { return m_WindowManager; }
    const WindowManager& GetWindowManager() const { return m_WindowManager; }
    WindowID GetMainWindowID() const { return m_MainWindowID; }

    // ========================================================================
    // Raven UI Context
    // ========================================================================
    // ApplicationはMain Window用UIContextのLifetimeだけを所有します。
    // Editor固有WidgetをApplicationへ持ち込まず、EditorLayer / Runtime Layerが必要な時だけ
    // Contextを借用してDrawListへ描画要求を積める境界にしています。
    //
    // Dear ImGuiは従来どおりm_ImGuiLayerで管理し続けるため、独自UI実装中も既存Editorを
    // 壊さず並行運用できます。
    UIContext& GetUIContext() { return m_UIContext; }
    const UIContext& GetUIContext() const { return m_UIContext; }

    // OS補助Window別のUIContext。Main Windowは従来のGetUIContext()を使用します。
    WindowID CreateUIWindow(const WindowSpecification& specification);
    // Root直下Widgetを新しいOS補助Windowへ移す一括入口。
    // Window生成または移譲に失敗した場合、Widgetの元の所有権を維持します。
    WindowID DetachUIRootChildToNewWindow(WindowID sourceID, UIElement* child,
        const WindowSpecification& specification);
    // Dock TabのContentを新しいOS WindowのRootへ移譲します。
    // Window生成が失敗した場合はDockのTab/Contentを変更しません。
    // Close時は元Dock Paneへの復帰を試み、元Paneが無効ならMain Rootへ戻します。
    WindowID DetachDockTabToNewWindow(WindowID sourceID, UIDockSpace& dock,
        std::uint64_t leafId, std::uint64_t tabId,
        const WindowSpecification& specification);
    // Layer更新や入力Callbackなど、Main UI Frame中から安全に切り離しを予約します。
    // 実際のWindow生成とTree移譲はMain EndFrame後に実行し、成功時にCallbackへIDを返します。
    using UIDetachCompleted = std::function<void(WindowID)>;
    // Layer更新中などのFrame内からDock Tabの切り離しを予約します。
    // 完了Callbackには新Window ID、失敗時には0を渡します。
    bool RequestDetachDockTabToNewWindow(WindowID sourceID, UIDockSpace& dock,
        std::uint64_t leafId, std::uint64_t tabId,
        const WindowSpecification& specification, UIDetachCompleted onCompleted = {});
    bool RequestDetachUIRootChildToNewWindow(WindowID sourceID, UIElement* child,
        const WindowSpecification& specification, UIDetachCompleted onCompleted = {});
    // 論理Windowの外側ドラッグを既存のFrame境界移譲Queueへ接続します。
    // UIWindowはRoot直下に配置してください。OS Windowの生成・所有はWindowManagerへ委譲します。
    bool BindUIWindowViewportTransfer(WindowID sourceID, UIWindow& window);
    // 補助WindowからMain Rootへの復帰をFrame境界で予約します。
    bool RequestAttachUIWindowToMain(WindowID sourceID, UIWindow* window,
        const math::Vec2& mainLocalDropPosition);
    UIContext* GetWindowUIContext(WindowID id);
    // Main/補助WindowのRoot直下Widgetを同じObjectのまま移譲します。
    // 補助WindowのClose時はRoot直下の通常WidgetをMainへ自動復帰させます。
    bool TransferUIRootChild(WindowID sourceID, WindowID destinationID, UIElement* child);

private:
    bool m_Running = true;
    std::unique_ptr<Window> m_Window;
    // Main Windowに紐づくFrame境界をApplicationが所有し、Windowより先に破棄します。
    Scope<RHISceneFrameLifecycle> m_SceneFrame;
    // ManagerはMain Windowを借用登録します。宣言順によりManagerが先に破棄されます。
    WindowManager m_WindowManager;
    WindowID m_MainWindowID = 0;
    std::vector<Scope<Layer>> m_Layers;
    Scope<Scene> m_scene;

    // Main Window用のRaven UI frame状態です。
    // Renderer backendは次段階でOpenGLUIRendererを実装した後、UIContext::SetRenderer()から
    // 注入します。それまではCPU側DrawList構築だけを安全に先行できます。
    UIContext m_UIContext;
    // UIContextより先に破棄し、Immediate Widgetの参照寿命を保ちます。
    UIImmediateContext m_ImmediateUI{ m_UIContext };
    bool m_PhysicsDebugImmediatePanelEnabled = false;
    Ref<UIFontAtlas> m_PhysicsDebugImmediateFont;
    std::unordered_map<WindowID, Scope<UIContext>> m_AuxiliaryUIContexts;
    struct DetachedDockTab
    {
        WindowID SourceID = 0;
        UIDockSpace* Dock = nullptr; // 復帰時は生存Treeと照合するまで参照しません。
        UIElement* Content = nullptr;
        std::uint64_t LeafID = 0u;
        std::uint64_t TabID = 0u;
        std::string Title;
        bool Closable = true;
    };
    std::unordered_map<WindowID, DetachedDockTab> m_DetachedDockTabs;
    struct PendingClosedUIChild
    {
        Scope<UIElement> Content;
        DetachedDockTab DockTab;
        bool HasDockTab = false;
    };
    // Main Frame中にWindowが閉じてもWidgetの所有権を失わず、EndFrame後に復帰します。
    std::vector<PendingClosedUIChild> m_PendingClosedUIChildren;
    void FlushPendingClosedUIChildren();
    struct PendingUIDetach
    {
        WindowID SourceID = 0;
        UIElement* Child = nullptr; // 実行前にRootの生存Child一覧と照合し、直接参照しません。
        WindowSpecification Specification;
        UIDetachCompleted OnCompleted;
    };
    std::vector<PendingUIDetach> m_PendingUIDetaches;
    struct PendingDockTabDetach
    {
        WindowID SourceID = 0;
        UIDockSpace* Dock = nullptr; // Flush時にRootから辿れる生存Elementと照合します。
        std::uint64_t LeafID = 0u;
        std::uint64_t TabID = 0u;
        WindowSpecification Specification;
        UIDetachCompleted OnCompleted;
    };
    std::vector<PendingDockTabDetach> m_PendingDockTabDetaches;
    struct PendingUIAttach
    {
        WindowID SourceID = 0;
        UIWindow* Child = nullptr; // Flush時にRootの生存Childと照合します。
        math::Vec2 MainLocalDropPosition{};
    };
    std::vector<PendingUIAttach> m_PendingUIAttaches;
    void FlushPendingUIDetaches();
    void CompleteReleasedUIWindowDrags();
    void OnAuxiliaryUIEvent(WindowID id, Event& event);
    bool m_RavenUIEnabled = true;

#if defined(_DEBUG)
    // Tree Mutation検証はCapture / Hover / PressedのContext状態も自己判定するため、
    // 構築時にMain Window用UIContextを借用します。Rootが検証TreeのLifetimeを所有します。
    bool m_UITreeMutationValidationAttached = false;
#endif

    // ImGuiLayerはLayerを継承しますが、Dear ImGuiのBegin/Endは全LayerのOnImGuiRender()を
    // 囲む特殊なframe境界なので、Applicationが専用参照を保持して順序を保証します。
    // またOpenGL backendをWindow/Contextより先にShutdownする責務もここで明示します。
    Scope<ImGuiLayer> m_ImGuiLayer;
};

} // namespace Raven
