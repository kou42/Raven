#include "Application.h"
#include "../Renderer/Renderer.h"
#include "Raven/ImGui/ImGuiLayer.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>

namespace Raven
{

Application::Application()
{
    // WindowはRenderer / ImGuiより先に生成します。
    // OpenGL ContextもWindow側で準備されるため、以降のGPU関連初期化より前である必要があります。
    m_Window = Window::Create();

    // OS/Window由来のEventをApplicationへ集約します。
    // Application::OnEvent()から後積みLayer優先で逆順伝播することで、
    // 将来的にEditor/GizmoがRuntime入力より先にEventを消費できる構造にしています。
    m_Window->SetEventCallback([this](Event& event)
        {
            OnEvent(event);
        });

    // Rendererは有効なGraphics Contextを必要とするため、Window生成後に初期化します。
    Renderer::Init();

    // ========================================================================
    // Dear ImGui lifecycle
    // ========================================================================
    // ImGuiLayerもRavenのLayerライフサイクルに近い形で管理しますが、通常Layer配列には積みません。
    // Dear ImGuiのBegin/Endは「全LayerのOnImGuiRender()を囲むframe境界」だからです。
    //
    // ApplicationがImGuiLayerへの専用参照を持つことで、必ず
    //
    //   ImGuiLayer::Begin()
    //       -> EditorLayer等のOnImGuiRender()
    //   ImGuiLayer::End()
    //
    // の順序を保証します。
    //
    // 重要:
    // Applicationが知るのはImGuiのframe境界までです。
    // Statistics / Hierarchy / Inspector等のEditor固有UIはEditorLayer以下へ分離し、
    // ApplicationへEditor固有分岐を増やさない方針とします。
    m_ImGuiLayer = CreateScope<ImGuiLayer>(*m_Window);
    m_ImGuiLayer->OnAttach();
}

Application::~Application()
{
    // ========================================================================
    // Shutdown order
    // ========================================================================
    // 通常Layerを先にDetachします。
    // EditorLayer::OnDetach()が将来ImGui関連リソースやEditor用GPUリソースへ触れる場合でも、
    // この時点ではImGui Context / Window / OpenGL Contextがまだ生存していることを保証します。
    for (auto& layer : m_Layers)
    {
        if (layer != nullptr)
        {
            layer->OnDetach();
        }
    }

    // ImGui OpenGL backendは有効なOpenGL Contextを必要とします。
    // そのためWindowが破棄される前に明示的にDetachし、backendとImGui Contextを終了します。
    if (m_ImGuiLayer != nullptr)
    {
        m_ImGuiLayer->OnDetach();
        m_ImGuiLayer.reset();
    }
}

void Application::PushLayer(Layer* layer)
{
#if 0
    // 旧raw pointer APIは所有権が曖昧になるため現在は使用しません。
    // Scope<Layer>版へ統一することで、ApplicationがLayerのLifetimeを明確に所有します。
    m_Layers.push_back(layer);
    layer->OnAttach();
#endif
}

void Application::PushLayer(Scope<Layer> layer)
{
    if (layer == nullptr)
    {
        return;
    }

    // Layerは登録された時点で利用可能な状態にします。
    // OnAttach()後に所有権をm_Layersへ移すことで、以降のUpdate/Render/Eventを
    // Applicationが一貫して管理します。
    layer->OnAttach();
    m_Layers.push_back(std::move(layer));
}

void Application::SetScene(Scope<Scene> scene)
{
    // Scene差し替え時は旧Sceneの終了処理を先に実行します。
    // EditorLayerはSceneポインタをキャッシュせずApplication::GetScene()を参照するため、
    // Scene交換後に古いSceneを参照し続けない構造になっています。
    if (m_scene != nullptr)
    {
        m_scene->OnDestroy();
    }

    m_scene = std::move(scene);

    if (m_scene != nullptr)
    {
        m_scene->OnCreate();
    }
}

void Application::Run()
{
    double previousTime = glfwGetTime();

    while (m_Running)
    {
        // 現段階ではEscapeをApplication終了入力として扱います。
        // EditorのShortcut/Input routingが増えた段階では、入力責務を再整理する余地があります。
        if (Input::IsKeyPressed(Key::Escape))
        {
            m_Running = false;
        }

        // ====================================================================
        // Frame timing
        // ====================================================================
        const double currentTime = glfwGetTime();
        float frameDeltaTime = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        // Debugger停止やWindow移動などで極端に大きなdtが入ると、AnimationやEditor更新が
        // 一気に進むため上限を設けます。Physics側はScene内部でfixed step処理します。
        frameDeltaTime = std::min(frameDeltaTime, 0.25f);

        // ====================================================================
        // Renderer statistics frame boundary
        // ====================================================================
        // Renderer統計はApplication frame単位で集計します。
        // Scene描画より前にResetすることで、Scene本体だけでなくPhysics / Animation Debug Overlayや
        // 後続Layerが発行した描画命令も同じframeのStatisticsとして集計できます。
        Renderer::BeginFrame();

        // ====================================================================
        // Runtime Scene
        // ====================================================================
        // Sceneはゲーム側のUpdate / Renderを担当します。
        // Editor処理はここへ混ぜず、後続のLayer更新へ分離します。
        if (m_scene != nullptr)
        {
            m_scene->OnUpdate(frameDeltaTime);
            m_scene->OnRender();
        }

        // ====================================================================
        // Application Layers
        // ====================================================================
        // EditorLayerを含む通常LayerのRuntime更新・描画です。
        // Dear ImGui frameとは独立しているため、OnRender()では通常Rendererを利用した
        // Editor用debug primitiveや将来のGizmo描画などを扱えます。
        for (auto& layer : m_Layers)
        {
            if (layer != nullptr)
            {
                layer->OnUpdate(frameDeltaTime);
                layer->OnRender();
            }
        }

        // ====================================================================
        // Dear ImGui frame
        // ====================================================================
        // Dear ImGuiは1 Application frameにつきBegin/Endを一度だけ実行します。
        // その間で各LayerへUI構築を依頼するため、EditorLayer側はImGui::Begin/Endによる
        // 個々のWindow構築だけに集中でき、backendのframe管理を知る必要がありません。
        if (m_ImGuiLayer != nullptr)
        {
            m_ImGuiLayer->Begin();

            for (auto& layer : m_Layers)
            {
                if (layer != nullptr)
                {
                    layer->OnImGuiRender(frameDeltaTime);
                }
            }

            m_ImGuiLayer->End();
        }

        // GLFW Event polling / Buffer swap等、Window側の1 frame終了処理を最後に行います。
        m_Window->OnUpdate();
    }
}

void Application::OnEvent(Event& event)
{
    // 現段階ではEvent確認用ログを残しています。
    // Editor入力が増えてログ量が問題になった場合はDebug Logger側へ移行する想定です。
    std::cout << event.ToString() << std::endl;

    // WindowCloseはApplication自身が処理すべき最上位Eventです。
    // 処理済みにしてLayer側へ不要な伝播を行わないようにします。
    if (event.GetEventType() == EventType::WindowClose)
    {
        m_Running = false;
        event.Handled = true;
    }

    // ========================================================================
    // Layer Event propagation
    // ========================================================================
    // Eventがまだ処理されていない場合だけLayerへ逆順伝播します。
    // 後から積まれたLayerほど前面にあるものとして先に入力を受け取るため、
    // EditorLayer / Overlay / Gizmo等がRuntime Layerより先に入力を消費できます。
    //
    // いずれかのLayerがevent.Handled = trueにした時点で伝播を終了します。
    for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
    {
        if (event.Handled)
        {
            break;
        }

        if (*it != nullptr)
        {
            (*it)->OnEvent(event);
        }
    }
}

} // namespace Raven
