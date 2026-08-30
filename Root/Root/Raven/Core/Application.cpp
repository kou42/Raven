#include "Application.h"
#include "../Renderer/Renderer.h"
#include "Raven/ImGui/ImGuiLayer.h"
#include "Raven/UI/Rendering/UIRenderer.h"

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
        m_UIContext.BeginFrame(math::Vec2(
            static_cast<float>(m_Window->GetWidth()),
            static_cast<float>(m_Window->GetHeight())));

#if defined(_DEBUG)
        // ====================================================================
        // Raven UI foundation validation overlay
        // ====================================================================
        // UIElement / UIPanelがまだ未実装のため、Renderer接続確認用に一時的な矩形を直接積みます。
        // 左上に半透明Panelが表示されれば、独自DrawList -> OpenGLUIRenderer -> GPUの経路が正常です。
        // UIElement導入後はこの検証コードを削除し、Editor側のRaven UI Treeへ置き換えます。
        //
        // 現在は描画経路の切り分け中なので、Dear ImGuiのMenu / DockSpaceと重ならず見落としにくいよう、
        // 画面中央付近へ完全不透明の大きなマゼンタ矩形を出します。
        // この色は通常のEditor配色と明確に異なるため、1 pixelでも描画されれば視認できます。
        const float uiViewportWidth = static_cast<float>(m_Window->GetWidth());
        const float uiViewportHeight = static_cast<float>(m_Window->GetHeight());
        const math::Vec2 validationMin(
            uiViewportWidth * 0.30f,
            uiViewportHeight * 0.30f);
        const math::Vec2 validationMax(
            uiViewportWidth * 0.70f,
            uiViewportHeight * 0.60f);

        m_UIContext.GetDrawList().AddRect(
            validationMin,
            validationMax,
            math::Vec4(1.0f, 0.0f, 1.0f, 1.0f));
#endif

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
