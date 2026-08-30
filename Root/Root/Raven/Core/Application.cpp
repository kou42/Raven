#include "Application.h"
#include "../Renderer/Renderer.h"
#include "Raven/ImGui/ImGuiLayer.h"
#include "Raven/UI/Rendering/UIRenderer.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UIPanel.h"

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
    // Raven UI renderer lifecycle
    // ========================================================================
    // Applicationは具体的なOpenGLUIRendererを直接生成しません。
    // UIRenderer::Create()が現在のRendererAPIに対応するbackendを選択するため、
    // Core層へOpenGL固有型を持ち込まずにMain Window用UIContextへ描画実装を注入できます。
    //
    // Dear ImGuiとは別Context / 別DrawListとして並行稼働させるため、既存Editorを維持したまま
    // 独自UIの描画・Layout・Inputを段階的に追加できます。
    m_UIContext.SetRenderer(UIRenderer::Create());

#if defined(_DEBUG)
    // ========================================================================
    // Raven UI retained-mode / layout / interaction validation panel
    // ========================================================================
    // GPU描画経路とRetained Treeに加え、UIButtonのNormal / Hovered / Pressed Visual Stateを確認します。
    // Mouse入力はWindow -> Core Event -> Application -> UIContextの経路で届くため、
    // pollingに依存せず実際の入力Eventと同じタイミングでHit Test / State遷移を検証できます。
    //
    // このTreeはApplication起動時に一度だけ生成され、以降はUIContextのRoot ElementがLifetimeを所有します。
    // Editor Widget導入後はApplication直下の検証Treeを削除し、各UI LayerがRoot以下へ必要なElementを構築します。
    auto validationPanel = CreateScope<UIPanel>();
    validationPanel->SetPosition(math::Vec2(24.0f, 48.0f));
    validationPanel->SetSize(math::Vec2(360.0f, 190.0f));
    validationPanel->SetBackgroundColor(math::Vec4(0.05f, 0.08f, 0.14f, 0.96f));
    validationPanel->SetLayoutMode(UILayoutMode::Vertical);
    validationPanel->SetPadding(12.0f);
    validationPanel->SetSpacing(10.0f);

    auto headerButton = CreateScope<UIButton>();
    headerButton->SetSize(math::Vec2(336.0f, 42.0f));
    headerButton->SetNormalColor(math::Vec4(0.10f, 0.28f, 0.55f, 1.0f));
    headerButton->SetHoveredColor(math::Vec4(0.16f, 0.40f, 0.72f, 1.0f));
    headerButton->SetPressedColor(math::Vec4(0.07f, 0.20f, 0.42f, 1.0f));
    headerButton->SetOnClick([]()
        {
            std::cout << "Raven UI validation button clicked" << std::endl;
        });
    validationPanel->AddChild(std::move(headerButton));

    auto horizontalRow = CreateScope<UIPanel>();
    horizontalRow->SetSize(math::Vec2(336.0f, 62.0f));
    horizontalRow->SetBackgroundColor(math::Vec4(0.08f, 0.11f, 0.18f, 1.0f));
    horizontalRow->SetLayoutMode(UILayoutMode::Horizontal);
    horizontalRow->SetPadding(UIThickness(8.0f, 9.0f));
    horizontalRow->SetSpacing(8.0f);

    auto leftPanel = CreateScope<UIPanel>();
    leftPanel->SetSize(math::Vec2(96.0f, 44.0f));
    leftPanel->SetBackgroundColor(math::Vec4(0.18f, 0.48f, 0.32f, 1.0f));
    horizontalRow->AddChild(std::move(leftPanel));

    auto centerPanel = CreateScope<UIPanel>();
    centerPanel->SetSize(math::Vec2(96.0f, 44.0f));
    centerPanel->SetBackgroundColor(math::Vec4(0.62f, 0.36f, 0.12f, 1.0f));
    horizontalRow->AddChild(std::move(centerPanel));

    auto rightPanel = CreateScope<UIPanel>();
    rightPanel->SetSize(math::Vec2(96.0f, 44.0f));
    rightPanel->SetBackgroundColor(math::Vec4(0.42f, 0.20f, 0.55f, 1.0f));
    horizontalRow->AddChild(std::move(rightPanel));

    validationPanel->AddChild(std::move(horizontalRow));

    auto footerPanel = CreateScope<UIPanel>();
    footerPanel->SetSize(math::Vec2(336.0f, 42.0f));
    footerPanel->SetBackgroundColor(math::Vec4(0.14f, 0.18f, 0.26f, 1.0f));
    validationPanel->AddChild(std::move(footerPanel));

    m_UIContext.GetRootElement().AddChild(std::move(validationPanel));
#endif

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
    // Application LayerはActive Sceneを借用している可能性があります。
    // Cloth/Jelly Demo LayerのようにOnDetach()でScene Entityを破棄するLayerがあるため、
    // Sceneより先にLayerをDetachして生成者自身へ破棄責務を返します。
    //
    // AttachはPushされた順に行われるため、終了時は逆順にDetachします。
    // 後から積まれたEditor/Overlayが前のLayerへ依存する場合でも、スタックと同じLIFOで解放することで
    // 依存先より先に依存元を終了できます。
    for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
    {
        if (*it != nullptr)
        {
            (*it)->OnDetach();
        }
    }

    // OnDetach()後もScopeを保持し続けるとLayer DestructorがImGui shutdown後まで遅延します。
    // Layerが保持するGPU/Editor Resourceを有効なContext中に解放するため、ここで所有権も破棄します。
    m_Layers.clear();

    // ========================================================================
    // Scene shutdown
    // ========================================================================
    // 1. virtual OnDestroy()ではSceneGame等の派生Sceneが、自身で直接所有するEntity Handleや
    //    Renderer Resource等の固有状態を整理します。
    // 2. 続く基底Scene::OnDestroy()ではScene内部LayerをLIFO順にOnDetach()してから、
    //    所有者が取りこぼした残存EntityをECS全体から最終Sweepします。
    //
    // 派生Scene側に「必ずScene::OnDestroy()を呼ぶ」という規約を要求しないことが重要です。
    // 新しいScene実装でbase呼び出しを忘れても、Applicationが共通の最終終了処理を保証します。
    if (m_scene != nullptr)
    {
        m_scene->OnDestroy();
        m_scene->Scene::OnDestroy();
        m_scene.reset();
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
    // ========================================================================
    // Scene replacement shutdown
    // ========================================================================
    // Scene差し替え時もApplication終了時と同じ二段階終了処理を使います。
    // 派生Scene固有Cleanupの後に、基底Sceneが内部LayerのDetachと残存Entity最終Sweepを実行してから
    // 所有権を入れ替えるため、古いSceneのEntity / Component参照を新しいSceneへ持ち越しません。
    if (m_scene != nullptr)
    {
        m_scene->OnDestroy();
        m_scene->Scene::OnDestroy();
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
        // CPUProfilerもRenderer::BeginFrame()と同じ境界で、次frame開始時に直前frameを確定します。
        // Scene描画より前にResetすることで、Scene本体だけでなくPhysics / Animation Debug Overlayや
        // 後続Layerが発行した描画命令も同じframeのStatisticsとして集計できます。
        Renderer::BeginFrame();

        // ====================================================================
        // Raven UI frame begin
        // ====================================================================
        // DrawListはRetained UI Treeとは別に毎frame再構築します。
        // BeginFrameをScene / Layer処理より前に置くことで、Runtime LayerとEditor Layerのどちらからも
        // GetUIContext().GetDrawList()へ描画要求を追加できる共通frame境界になります。
        //
        // 現在はUIContextがRoot UIElementを所有しているため、通常WidgetはDrawListへ直接書かず、
        // Root以下のRetained Treeを更新します。EndFrame()時にTreeからDrawListへ自動展開されます。
        m_UIContext.BeginFrame(math::Vec2(
            static_cast<float>(m_Window->GetWidth()),
            static_cast<float>(m_Window->GetHeight())));

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

        // ====================================================================
        // Raven UI frame end / overlay rendering
        // ====================================================================
        // 移行期間はRaven UIをDear ImGuiの後へ描画し、既存Editor Windowに隠れず結果を確認できる
        // Overlayとして扱います。将来Game UI用Contextを分離した段階では、Game View用RenderTargetへ
        // 別Contextを描くことでEditor UIとの描画順も明確に分離します。
        m_UIContext.EndFrame();

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
    // Raven UI Mouse Event routing
    // ========================================================================
    // Platform Mouse EventをUIContextのHit Test / Bubble Routingへ変換します。
    // Button Eventも入力発生時の座標を自身に保持するため、ApplicationはInput pollingを行わず
    // Event snapshotだけからUI routingできます。これにより入力時刻と座標の対応を維持します。
    // UIEvent側でHandledになった場合だけCore EventもHandledとして、背後LayerへのClick-throughを防ぎます。
    if (event.Handled == false && event.GetEventType() == EventType::MouseMoved)
    {
        MouseMovedEvent& mouseEvent = static_cast<MouseMovedEvent&>(event);
        event.Handled = m_UIContext.RouteMouseMove(math::Vec2(mouseEvent.GetX(), mouseEvent.GetY()));
    }
    else if (event.Handled == false && event.GetEventType() == EventType::MouseButtonPressed)
    {
        MouseButtonPressedEvent& mouseEvent = static_cast<MouseButtonPressedEvent&>(event);

        UIMouseButton uiButton = UIMouseButton::None;
        if (mouseEvent.GetMouseButton() == GLFW_MOUSE_BUTTON_LEFT)
        {
            uiButton = UIMouseButton::Left;
        }
        else if (mouseEvent.GetMouseButton() == GLFW_MOUSE_BUTTON_RIGHT)
        {
            uiButton = UIMouseButton::Right;
        }
        else if (mouseEvent.GetMouseButton() == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            uiButton = UIMouseButton::Middle;
        }

        if (uiButton != UIMouseButton::None)
        {
            event.Handled = m_UIContext.RouteMouseDown(
                math::Vec2(mouseEvent.GetX(), mouseEvent.GetY()), uiButton);
        }
    }
    else if (event.Handled == false && event.GetEventType() == EventType::MouseButtonReleased)
    {
        MouseButtonReleasedEvent& mouseEvent = static_cast<MouseButtonReleasedEvent&>(event);

        UIMouseButton uiButton = UIMouseButton::None;
        if (mouseEvent.GetMouseButton() == GLFW_MOUSE_BUTTON_LEFT)
        {
            uiButton = UIMouseButton::Left;
        }
        else if (mouseEvent.GetMouseButton() == GLFW_MOUSE_BUTTON_RIGHT)
        {
            uiButton = UIMouseButton::Right;
        }
        else if (mouseEvent.GetMouseButton() == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            uiButton = UIMouseButton::Middle;
        }

        if (uiButton != UIMouseButton::None)
        {
            event.Handled = m_UIContext.RouteMouseUp(
                math::Vec2(mouseEvent.GetX(), mouseEvent.GetY()), uiButton);
        }
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
