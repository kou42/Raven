#pragma once
#include "Raven/Core/Window.h"
#include "Raven/Core/WindowManager.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"

#include "Raven/Scene/Scene.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Core/Event.h"
#include "Raven/UI/Core/UIContext.h"

#if defined(_DEBUG)
#include "Raven/UI/Debug/UITreeMutationValidation.h"
#endif

#include <memory>
#include <unordered_map>
#include <iostream>

namespace Raven
{

class ImGuiLayer;

struct ApplicationSpecification
{
    WindowProps WindowProperties{};
    bool EnableRavenUI = true;
    bool EnableDearImGui = true;
};

class Application
{
public:
    Application();
    explicit Application(const ApplicationSpecification& specification);
    ~Application();

    void Run();
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
    UIContext* GetWindowUIContext(WindowID id);
    // Main/補助WindowのRoot直下Widgetを同じObjectのまま移譲します。
    // Windowを閉じる前にMainへ戻す場合は明示的に呼び出してください。
    bool TransferUIRootChild(WindowID sourceID, WindowID destinationID, UIElement* child);

private:
    bool m_Running = true;
    std::unique_ptr<Window> m_Window;
    // ManagerはMain Windowを借用登録します。宣言順によりManagerが先に破棄されます。
    WindowManager m_WindowManager;
    WindowID m_MainWindowID = 0;
    std::vector<Scope<Layer>> m_Layers;
    Scope<Scene> m_scene;

    // Main Window用のRaven UI frame状態です。
    // Renderer backendは次段階でOpenGLUIRendererを実装した後、UIContext::SetRenderer()から
    // 注入します。それまではCPU側DrawList構築だけを安全に先行できます。
    UIContext m_UIContext;
    std::unordered_map<WindowID, Scope<UIContext>> m_AuxiliaryUIContexts;
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
