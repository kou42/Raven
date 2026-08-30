#pragma once
#include "Raven/Core/Window.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"

#include "Raven/Scene/Scene.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Core/Event.h"
#include "Raven/UI/Core/UIContext.h"

#include <memory>
#include <iostream>

namespace Raven
{

class ImGuiLayer;

class Application
{
public:
    Application();
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

private:
    bool m_Running = true;
    std::unique_ptr<Window> m_Window;
    std::vector<Scope<Layer>> m_Layers;
    Scope<Scene> m_scene;

    // Main Window用のRaven UI frame状態です。
    // Renderer backendは次段階でOpenGLUIRendererを実装した後、UIContext::SetRenderer()から
    // 注入します。それまではCPU側DrawList構築だけを安全に先行できます。
    UIContext m_UIContext;

    // ImGuiLayerはLayerを継承しますが、Dear ImGuiのBegin/Endは全LayerのOnImGuiRender()を
    // 囲む特殊なframe境界なので、Applicationが専用参照を保持して順序を保証します。
    // またOpenGL backendをWindow/Contextより先にShutdownする責務もここで明示します。
    Scope<ImGuiLayer> m_ImGuiLayer;
};

} // namespace Raven
