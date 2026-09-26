#include "Application.h"
#include "Raven/Scene/SceneGame.h"
#include "Raven/Physics/Debug/PhysicsDebugImmediatePanel.h"
#include "../Renderer/Renderer.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"
#include "Raven/ImGui/ImGuiLayer.h"
#include "Raven/UI/Rendering/UIRenderer.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UIPanel.h"
#include "Raven/UI/Widgets/UISlider.h"
#include "Raven/UI/Widgets/UISplitter.h"
#include "Raven/UI/Widgets/UIInputText.h"
#include "Raven/UI/Widgets/UIWindow.h"
#include "Raven/UI/Docking/UIDockSpace.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>

namespace Raven
{

namespace
{
// 通常ApplicationとExplicit Sceneで同じFrame時間の規則を使用します。
// 時計の巻き戻りは0秒に丸め、Debugger停止やWindow移動による長時間停止は
// 0.25秒に制限してPhysics/Animationへ過大なdtを渡しません。
float CalculateFrameDeltaTime(double currentTime, double& previousTime)
{
    const double elapsed = currentTime - previousTime;
    previousTime = currentTime;
    return static_cast<float>(std::clamp(elapsed, 0.0, 0.25));
}

UIKey ToUIKey(int keyCode)
{
    switch (keyCode)
    {
    case GLFW_KEY_TAB: return UIKey::Tab;
    case GLFW_KEY_ESCAPE: return UIKey::Escape;
    case GLFW_KEY_ENTER: return UIKey::Enter;
    case GLFW_KEY_SPACE: return UIKey::Space;
    case GLFW_KEY_LEFT: return UIKey::Left;
    case GLFW_KEY_RIGHT: return UIKey::Right;
    case GLFW_KEY_HOME: return UIKey::Home;
    case GLFW_KEY_END: return UIKey::End;
    case GLFW_KEY_UP: return UIKey::Up;
    case GLFW_KEY_DOWN: return UIKey::Down;
    case GLFW_KEY_BACKSPACE: return UIKey::Backspace;
    case GLFW_KEY_DELETE: return UIKey::Delete;
    case GLFW_KEY_A: return UIKey::A;
    case GLFW_KEY_C: return UIKey::C;
    case GLFW_KEY_V: return UIKey::V;
    case GLFW_KEY_X: return UIKey::X;
    case GLFW_KEY_Y: return UIKey::Y;
    case GLFW_KEY_Z: return UIKey::Z;
    default: return UIKey::Unknown;
    }
}
} // namespace

Application::Application()
    : Application(ApplicationSpecification{})
{
}

Application::Application(const ApplicationSpecification& specification)
    : m_RavenUIEnabled(specification.EnableRavenUI)
    , m_PhysicsDebugImmediatePanelEnabled(specification.EnablePhysicsDebugImmediatePanel)
    , m_PhysicsDebugImmediateFont(specification.PhysicsDebugImmediateFont)
{
    // WindowはRenderer / ImGuiより先に生成します。
    // 選択BackendのGraphics ContextもWindow側で準備されるため、以降のGPU関連初期化より前である必要があります。
    m_Window = Window::Create(specification.WindowProperties);
    if (m_Window == nullptr)
    {
        m_Running = false;
        return;
    }

    // Main Windowの所有権は従来どおりApplicationが保持し、Managerには借用登録します。
    m_MainWindowID = m_WindowManager.RegisterWindow(*m_Window);

    // OS/Window由来のEventをApplicationへ集約します。
    // Application::OnEvent()から後積みLayer優先で逆順伝播することで、
    // 将来的にEditor/GizmoがRuntime入力より先にEventを消費できる構造にしています。
    m_Window->SetEventCallback([this](Event& event)
        {
            OnEvent(event);
        });

    // プログラムからのSetFocus/SetText/Tree変更もUIContext経由でOS側IMEへ同期します。
    m_UIContext.SetIMECancelCallback([this]()
        {
            if (m_Window != nullptr)
            {
                m_Window->CancelIMEComposition();
            }
        });

    // WindowsのIME候補Windowが必要とするCaret座標は、Focus中のUIInputTextだけが提供します。
    // WindowへUI型を依存させずApplicationで橋渡しし、他Widget/ImGuiではOS既定の位置を維持します。
    m_Window->SetIMECaretPositionCallback([this](float& x, float& y)
        {
            if (m_RavenUIEnabled == false)
            {
                return false;
            }
            UIInputText* input = dynamic_cast<UIInputText*>(m_UIContext.GetFocusedElement());
            if (input == nullptr)
            {
                return false;
            }
            const math::Vec2 caret = input->GetIMECaretScreenPosition();
            x = caret.x;
            y = caret.y;
            return true;
        });

    // RendererはWindowと同じBackendを明示的に受け取ります。
    // Legacy CommandList未対応BackendをOpenGLへ暗黙fallbackせず、安全に起動を中止します。
    if (Renderer::TryInit(m_Window->GetBackend()) == false)
    {
        m_Running = false;
        return;
    }

    // Window / Renderer初期化後にFrame境界を確定します。未対応Backendを成功扱いせず、
    // SceneやUIの生成前に失敗を検出します。Windowの所有権は移譲しません。
    m_SceneFrame = RHISceneFrameLifecycle::Create(*m_Window);
    if (m_SceneFrame == nullptr)
    {
        m_Running = false;
        return;
    }

    // ========================================================================
    // Raven UI renderer lifecycle
    // ========================================================================
    // Applicationは具体的なOpenGLUIRendererを直接生成しません。
    // UIRenderer::Create()が現在のRendererAPIに対応するbackendを選択するため、
    // Core層へOpenGL固有型を持ち込まずにMain Window用UIContextへ描画実装を注入できます。
    //
    // Dear ImGuiとは別Context / 別DrawListとして並行稼働させるため、既存Editorを維持したまま
    // 独自UIの描画・Layout・Inputを段階的に追加できます。
    if (m_RavenUIEnabled == true)
    {
        m_UIContext.SetRenderer(UIRenderer::Create(m_Window->GetBackend()));
    }

#if defined(_DEBUG)
    if (m_RavenUIEnabled == true)
    {
        m_UITreeMutationValidationAttached =
            m_UIContext.GetRootElement().AddChild(
                UITreeMutationValidation::Create(m_UIContext)) != nullptr;

    // ========================================================================
    // Raven UI retained-mode / layout / interaction validation panel
    // ========================================================================
    // GPU描画経路とRetained Treeに加え、Button / Slider / SplitterのInteractionを確認します。
    // Mouse入力はWindow -> Core Event -> Application -> UIContextの経路で届くため、
    // pollingに依存せず実際の入力Eventと同じタイミングでHit Test / Capture / State遷移を検証できます。
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
    headerButton->SetFocusable(true);
    headerButton->SetNormalColor(math::Vec4(0.10f, 0.28f, 0.55f, 1.0f));
    headerButton->SetHoveredColor(math::Vec4(0.16f, 0.40f, 0.72f, 1.0f));
    headerButton->SetPressedColor(math::Vec4(0.07f, 0.20f, 0.42f, 1.0f));
    headerButton->SetFocusedColor(math::Vec4(0.24f, 0.48f, 0.86f, 1.0f));
    headerButton->SetOnClick([]()
        {
            std::cout << "Raven UI validation button clicked" << std::endl;
        });
    validationPanel->AddChild(std::move(headerButton));

    // Splitter検証Rowです。
    // Splitter自身はPanelを知らずDrag差分だけを返し、利用側が左右PanelのPreferredSizeへ反映します。
    // この接続方式をそのままEditorのHierarchy / Inspector / Game View等へ再利用できます。
    auto horizontalRow = CreateScope<UIPanel>();
    horizontalRow->SetSize(math::Vec2(336.0f, 62.0f));
    horizontalRow->SetBackgroundColor(math::Vec4(0.08f, 0.11f, 0.18f, 1.0f));
    horizontalRow->SetLayoutMode(UILayoutMode::Horizontal);
    horizontalRow->SetPadding(UIThickness(8.0f, 9.0f));
    horizontalRow->SetSpacing(8.0f);

    auto leftPanel = CreateScope<UIPanel>();
    leftPanel->SetSize(math::Vec2(120.0f, 44.0f));
    leftPanel->SetMinSize(math::Vec2(72.0f, 44.0f));
    leftPanel->SetMaxSize(math::Vec2(224.0f, 44.0f));
    leftPanel->SetBackgroundColor(math::Vec4(0.18f, 0.48f, 0.32f, 1.0f));
    UIPanel* leftPanelElement = leftPanel.get();
    horizontalRow->AddChild(std::move(leftPanel));

    auto splitter = CreateScope<UISplitter>();
    splitter->SetSize(math::Vec2(8.0f, 44.0f));
    splitter->SetOrientation(UISplitterOrientation::Vertical);

    auto rightPanel = CreateScope<UIPanel>();
    rightPanel->SetSize(math::Vec2(176.0f, 44.0f));
    rightPanel->SetMinSize(math::Vec2(72.0f, 44.0f));
    rightPanel->SetMaxSize(math::Vec2(224.0f, 44.0f));
    rightPanel->SetBackgroundColor(math::Vec4(0.42f, 0.20f, 0.55f, 1.0f));
    UIPanel* rightPanelElement = rightPanel.get();

    splitter->SetOnDragDelta([leftPanelElement, rightPanelElement](float delta)
        {
            if (leftPanelElement == nullptr || rightPanelElement == nullptr)
            {
                return;
            }

            // Row content幅320pxからSplitter 8pxとSpacing 16pxを除いた296pxを左右Panelで共有します。
            // 片側だけをResizeするとRow全体の幅が変化するため、反対側へ同量を返して合計幅を維持します。
            constexpr float panelTotalWidth = 296.0f;
            const float currentLeftWidth = leftPanelElement->GetPreferredSize().x;
            const float newLeftWidth = std::clamp(currentLeftWidth + delta, 72.0f, 224.0f);
            const float newRightWidth = panelTotalWidth - newLeftWidth;

            leftPanelElement->SetPreferredSize(math::Vec2(newLeftWidth, 44.0f));
            rightPanelElement->SetPreferredSize(math::Vec2(newRightWidth, 44.0f));
        });

    horizontalRow->AddChild(std::move(splitter));
    horizontalRow->AddChild(std::move(rightPanel));
    validationPanel->AddChild(std::move(horizontalRow));

    // SliderはCapture中にElement外へPointerが出ても値更新を継続します。
    // Focus Lost時にはApplicationがCancelMouseCapture()を呼ぶため、Mouse Upが戻らなくてもDraggingは残りません。
    auto footerSlider = CreateScope<UISlider>();
    footerSlider->SetSize(math::Vec2(336.0f, 42.0f));
    footerSlider->SetFocusable(true);
    footerSlider->SetRange(0.0f, 1.0f);
    footerSlider->SetValue(0.35f);
    footerSlider->SetKeyboardStep(0.05f);
    footerSlider->SetOnValueChanged([](float value)
        {
            std::cout << "Raven UI validation slider: " << value << std::endl;
        });
    validationPanel->AddChild(std::move(footerSlider));

    m_UIContext.GetRootElement().AddChild(std::move(validationPanel));

    // Phase 6 / 12 接続検証用の論理Windowです。既存Editorは変更せず、
    // タイトルバー移動・右下Resize・画面外Releaseによる補助Window生成を確認できます。
    auto logicalWindow = CreateScope<UIWindow>();
    logicalWindow->SetTitle("Raven UI Floating Window");
    logicalWindow->SetPosition(math::Vec2(420.0f, 48.0f));
    logicalWindow->SetSize(math::Vec2(320.0f, 240.0f));
    UIWindow* logicalWindowHandle = logicalWindow.get();
    if (m_UIContext.AddRootChild(std::move(logicalWindow)) != nullptr)
    {
        BindUIWindowViewportTransfer(m_MainWindowID, *logicalWindowHandle);
    }
    }
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
    if (specification.EnableDearImGui == true)
    {
        Scope<ImGuiLayer> imguiLayer = CreateScope<ImGuiLayer>(*m_Window);
        imguiLayer->OnAttach();
        if (imguiLayer->IsInitialized() == true)
        {
            // 初期化済みContextだけをFrame Loopへ公開し、Backend未対応時のUI呼び出しを防ぎます。
            m_ImGuiLayer = std::move(imguiLayer);
        }
    }
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
    // Sceneの二段階CleanupはSceneManagerへ集約します。
    m_SceneManager.Shutdown();

    // ImGui OpenGL backendは有効なOpenGL Contextを必要とします。
    // そのためWindowが破棄される前に明示的にDetachし、backendとImGui Contextを終了します。
    if (m_ImGuiLayer != nullptr)
    {
        m_ImGuiLayer->OnDetach();
        m_ImGuiLayer.reset();
    }

    // 補助WindowはMain WindowのOpenGL ContextとGPU資産を共有します。
    // Main Windowが破棄される前に補助WindowのCleanup/VAO解放を完了させます。
    // 外部参照が残る場合はShutdownOwnedWindowsがfalseを返すため、
    // 補助Windowの利用側はOnDetach()で専用VAO/FBO参照を解放してください。
    const bool auxiliaryWindowsClosed = m_WindowManager.ShutdownOwnedWindows();
    m_AuxiliaryUIContexts.clear();
    FlushPendingClosedUIChildren();
    assert(auxiliaryWindowsClosed == true);

    // Frame境界が借用するMain Windowより先にBackend側のFrame状態を解放します。
    m_SceneFrame.reset();
}

WindowID Application::CreateUIWindow(const WindowSpecification& specification)
{
    if (m_RavenUIEnabled == false || specification.Backend != RHIBackend::OpenGL ||
        m_Window->GetBackend() != RHIBackend::OpenGL)
    {
        // 他BackendのMulti-Viewportは描画Target接続後に有効化します。
        return 0;
    }
    auto callbackID = std::make_shared<WindowID>(0);
    const WindowID id = m_WindowManager.CreateManagedWindow(specification,
        [this, callbackID](Event& event)
        {
            if (*callbackID != 0)
            {
                OnAuxiliaryUIEvent(*callbackID, event);
            }
        });
    *callbackID = id;
    if (id == 0)
    {
        return 0;
    }
    Window* window = m_WindowManager.GetWindow(id);
    if (window == nullptr || window->MakeContextCurrent() == false)
    {
        m_WindowManager.UnregisterWindow(id);
        m_Window->MakeContextCurrent();
        return 0;
    }
    auto context = CreateScope<UIContext>();
    // UIの見た目とユーザー指定倍率はOS Windowごとに初期化される値ではないため、
    // Main Windowの設定を引き継ぎます。OS由来のDPIだけは各WindowのBeginFrameで同期します。
    context->SetTheme(m_UIContext.GetTheme());
    context->SetUserScale(m_UIContext.GetUserScale());
    // VAOはContext間で共有されないため、補助WindowをCurrentにしてRendererを生成します。
    context->SetRenderer(UIRenderer::Create(window->GetBackend()));
    m_AuxiliaryUIContexts.emplace(id, std::move(context));
    // UIContextはWindowManagerのCloseCleanupで破棄し、GL資産の寿命をOS Windowより短くします。
    const bool restored = m_Window->MakeContextCurrent();
    // CleanupをLifecycleより先に登録し、途中失敗でもWindow破棄前にRendererを解放します。
    const bool cleanupRegistered = m_WindowManager.SetWindowCloseCleanup(id,
        [this, id](Window&)
        {
            const auto it = m_AuxiliaryUIContexts.find(id);
            if (it == m_AuxiliaryUIContexts.end())
            {
                return;
            }

            // OS補助Windowを閉じても通常Widgetの所有権を失わないようMainへ戻します。
            // DetachChildが旧WindowのCapture/Focus/IMEを解除します。
            // Rootの内部Popup/TooltipはContext固有なので移動せずContextと共に破棄します。
            UIContext& source = *it->second;
            // MainのFrame中でもWindow Closeは発生し得ます。Widgetは破棄せず一時退避し、
            // 元Dockへの復帰やMain Root追加はMain EndFrame後にまとめて実行します。
            DetachedDockTab dockRecord;
            bool hasDockRecord = false;
            const auto detached = m_DetachedDockTabs.find(id);
            if (detached != m_DetachedDockTabs.end())
            {
                dockRecord = detached->second;
                hasDockRecord = true;
                m_DetachedDockTabs.erase(detached);
            }
            std::vector<UIElement*> children;
            for (const auto& child : source.GetRootElement().GetChildren())
            {
                if (child != nullptr)
                {
                    children.push_back(child.get());
                }
            }
            for (UIElement* child : children)
            {
                Scope<UIElement> content = source.DetachRootChild(child);
                if (content != nullptr)
                {
                    const bool isDockContent =
                        hasDockRecord == true && child == dockRecord.Content;
                    m_PendingClosedUIChildren.push_back(PendingClosedUIChild{
                        std::move(content), isDockContent ? dockRecord : DetachedDockTab{},
                        isDockContent });
                }
            }

            // Window破棄前、所属OpenGL ContextがCurrentな間にRendererを破棄します。
            m_AuxiliaryUIContexts.erase(it);
        });
    if (restored == false || cleanupRegistered == false ||
        m_WindowManager.AttachFrameLifecycle(id) == false)
    {
        // 登録失敗時も補助WindowのContextでRendererを破棄します。
        window->MakeContextCurrent();
        m_AuxiliaryUIContexts.erase(id);
        m_WindowManager.UnregisterWindow(id);
        m_Window->MakeContextCurrent();
        return 0;
    }
    // 補助Window側にもMain Windowと同じIME取消/Caret通知を結びます。
    // CallbackはWindow破棄と同時に消えるため、WindowIDでContextの生存を確認します。
    UIContext* auxiliaryUI = GetWindowUIContext(id);
    auxiliaryUI->SetIMECancelCallback([this, id]()
        {
            Window* target = m_WindowManager.GetWindow(id);
            if (target != nullptr)
            {
                target->CancelIMEComposition();
            }
        });
    window->SetIMECaretPositionCallback([this, id](float& x, float& y)
        {
            UIContext* target = GetWindowUIContext(id);
            if (target == nullptr)
            {
                return false;
            }
            UIInputText* input = dynamic_cast<UIInputText*>(target->GetFocusedElement());
            if (input == nullptr)
            {
                return false;
            }
            const math::Vec2 caret = input->GetIMECaretScreenPosition();
            x = caret.x;
            y = caret.y;
            return true;
        });
    return id;
}

WindowID Application::DetachDockTabToNewWindow(
    WindowID sourceID, UIDockSpace& dock, std::uint64_t leafId,
    std::uint64_t tabId, const WindowSpecification& specification)
{
    UIContext* source = GetWindowUIContext(sourceID);
    if (m_RavenUIEnabled == false || source == nullptr ||
        dock.GetContext() != source || source->IsFrameActive() == true ||
        m_WindowManager.IsWindowClosePending(sourceID) == true)
    {
        return 0;
    }
    UITabView* view = dock.GetTabView(leafId);
    if (view == nullptr || view->GetModel().FindTab(tabId) == nullptr ||
        view->GetTabContent(tabId) == nullptr)
    {
        return 0;
    }

    // Dock Modelを変更する前にWindow生成を完了させます。
    // OS Window生成失敗時はTab Contentと選択状態を元のPaneに残します。
    const WindowID destinationID = CreateUIWindow(specification);
    if (destinationID == 0)
    {
        return 0;
    }
    UIContext* destination = GetWindowUIContext(destinationID);
    if (destination == nullptr)
    {
        m_WindowManager.UnregisterWindow(destinationID);
        return 0;
    }
    destination->SetTheme(source->GetTheme());
    destination->SetUserScale(source->GetUserScale());

    UITabItem tab;
    Scope<UIElement> content = dock.ExtractTabForWindow(leafId, tabId, tab);
    if (content == nullptr)
    {
        m_WindowManager.UnregisterWindow(destinationID);
        return 0;
    }
    // 非選択TabはContentが非表示なので、新Windowへ渡す前に表示状態を戻します。
    content->SetVisible(true);
    UIElement* raw = content.get();
    if (destination->AddRootChild(std::move(content)) != raw)
    {
        // 通常は到達しません。Root追加失敗でも空Windowは残しません。
        m_WindowManager.UnregisterWindow(destinationID);
        return 0;
    }
    // Rootへの追加が完了してから登録し、失敗した生成経路を復帰対象にしません。
    m_DetachedDockTabs[destinationID] = DetachedDockTab{
        sourceID, &dock, raw, leafId, tab.Id, tab.Title, tab.Closable };
    return destinationID;
}

WindowID Application::DetachUIRootChildToNewWindow(
    WindowID sourceID, UIElement* child, const WindowSpecification& specification)
{
    UIContext* source = GetWindowUIContext(sourceID);
    if (source == nullptr || child == nullptr ||
        child->GetParent() != &source->GetRootElement() ||
        source->IsFrameActive() == true ||
        m_WindowManager.IsWindowClosePending(sourceID) == true)
    {
        return 0;
    }

    // OS Windowを先に確保し、成功した場合だけ既存Widgetの所有権を移します。
    // 失敗時は空の補助Windowだけを破棄し、元のUI Treeを変更しません。
    const WindowID destinationID = CreateUIWindow(specification);
    if (destinationID == 0)
    {
        return 0;
    }
    // Main以外の補助Windowから切り離す場合も移動元のTheme/ユーザー倍率を継承します。
    // 新Windowはまだ空なので、Widgetを追加する前に設定を確定させます。
    UIContext* destination = GetWindowUIContext(destinationID);
    if (destination != nullptr)
    {
        destination->SetTheme(source->GetTheme());
        destination->SetUserScale(source->GetUserScale());
    }
    if (TransferUIRootChild(sourceID, destinationID, child) == false)
    {
        m_WindowManager.UnregisterWindow(destinationID);
        return 0;
    }
    return destinationID;
}

bool Application::RequestDetachDockTabToNewWindow(
    WindowID sourceID, UIDockSpace& dock, std::uint64_t leafId,
    std::uint64_t tabId, const WindowSpecification& specification,
    UIDetachCompleted onCompleted)
{
    UIContext* source = GetWindowUIContext(sourceID);
    if (m_RavenUIEnabled == false || source == nullptr ||
        dock.GetContext() != source ||
        m_WindowManager.IsWindowClosePending(sourceID) == true)
    {
        return false;
    }
    UITabView* view = dock.GetTabView(leafId);
    if (view == nullptr || view->GetModel().FindTab(tabId) == nullptr ||
        view->GetTabContent(tabId) == nullptr)
    {
        return false;
    }
    for (const PendingDockTabDetach& pending : m_PendingDockTabDetaches)
    {
        if (pending.SourceID == sourceID && pending.Dock == &dock &&
            pending.LeafID == leafId && pending.TabID == tabId)
        {
            return false;
        }
    }
    m_PendingDockTabDetaches.push_back(PendingDockTabDetach{
        sourceID, &dock, leafId, tabId, specification, std::move(onCompleted) });
    return true;
}

bool Application::RequestDetachUIRootChildToNewWindow(
    WindowID sourceID, UIElement* child, const WindowSpecification& specification,
    UIDetachCompleted onCompleted)
{
    UIContext* source = GetWindowUIContext(sourceID);
    if (m_RavenUIEnabled == false || source == nullptr || child == nullptr ||
        m_WindowManager.IsWindowClosePending(sourceID) == true)
    {
        return false;
    }

    // Pointerを直接逆参照せず、所有Rootの生存Childと同一性を照合します。
    // Frame中のWidget削除や二重予約はFlush時にも再検証します。
    bool found = false;
    for (const auto& item : source->GetRootElement().GetChildren())
    {
        if (item.get() == child)
        {
            found = true;
            break;
        }
    }
    if (found == false)
    {
        return false;
    }
    for (const PendingUIDetach& pending : m_PendingUIDetaches)
    {
        if (pending.SourceID == sourceID && pending.Child == child)
        {
            return false;
        }
    }
    m_PendingUIDetaches.push_back(
        PendingUIDetach{ sourceID, child, specification, std::move(onCompleted) });
    return true;
}

void Application::FlushPendingClosedUIChildren()
{
    if (m_UIContext.IsFrameActive() == true)
    {
        return;
    }
    // 復帰先のDockが削除済みでも生ポインタを逆参照せず、元Contextの生存Treeで確認します。
    // Close済みの別Windowへは戻さずMain Rootへ退避させます。
    std::vector<PendingClosedUIChild> pending;
    pending.swap(m_PendingClosedUIChildren);
    for (PendingClosedUIChild& entry : pending)
    {
        bool restored = false;
        if (entry.HasDockTab == true)
        {
            const DetachedDockTab& record = entry.DockTab;
            UIContext* original = GetWindowUIContext(record.SourceID);
            if (original != nullptr && original->IsFrameActive() == false &&
                m_WindowManager.IsWindowClosePending(record.SourceID) == false)
            {
                const auto isAlive = [&](const auto& self, const UIElement& parent) -> bool
                {
                    for (const auto& child : parent.GetChildren())
                    {
                        if (child.get() == record.Dock)
                        {
                            return true;
                        }
                        if (self(self, *child) == true)
                        {
                            return true;
                        }
                    }
                    return false;
                };
                if (isAlive(isAlive, original->GetRootElement()) == true)
                {
                    UITabView* view = record.Dock->GetTabView(record.LeafID);
                    const UIDockNode* leaf =
                        record.Dock->GetLayout().FindNode(record.LeafID);
                    if (view != nullptr && leaf != nullptr &&
                        leaf->GetTabs() != nullptr &&
                        view->GetModel().FindTab(record.TabID) == nullptr &&
                        leaf->GetTabs()->FindTab(record.TabID) == nullptr &&
                        entry.Content != nullptr &&
                        entry.Content->GetParent() == nullptr &&
                        entry.Content->GetContext() == nullptr)
                    {
                        // AddTabはScopeを受け取るため、事前に移動先Modelを検証します。
                        // 正常系ではContentを破棄せず元のDock Tabとして復元します。
                        restored = record.Dock->AddTab(record.LeafID, record.TabID,
                            record.Title, std::move(entry.Content), record.Closable);
                    }
                }
            }
        }
        if (restored == false && entry.Content != nullptr)
        {
            entry.Content->SetVisible(true);
            m_UIContext.AddRootChild(std::move(entry.Content));
        }
    }
}

void Application::FlushPendingUIDetaches()
{
    FlushPendingClosedUIChildren();
    // Callbackから次の予約が追加されても反復中のvectorを変更しないよう入れ替えます。
    std::vector<PendingUIDetach> pending;
    pending.swap(m_PendingUIDetaches);
    // Dock自体が予約後に破棄される可能性があるため、ポインタを逆参照する前に
    // Source Rootの生存Treeを探索します。Callbackから追加された予約は次Frameへ送ります。
    std::vector<PendingDockTabDetach> dockPending;
    dockPending.swap(m_PendingDockTabDetaches);
    for (PendingDockTabDetach& request : dockPending)
    {
        WindowID result = 0;
        UIContext* source = GetWindowUIContext(request.SourceID);
        if (source != nullptr && source->IsFrameActive() == false &&
            m_WindowManager.IsWindowClosePending(request.SourceID) == false)
        {
            const auto isAlive = [&](const auto& self, const UIElement& parent) -> bool
            {
                for (const auto& child : parent.GetChildren())
                {
                    if (child.get() == request.Dock)
                    {
                        return true;
                    }
                    if (self(self, *child) == true)
                    {
                        return true;
                    }
                }
                return false;
            };
            if (isAlive(isAlive, source->GetRootElement()) == true)
            {
                result = DetachDockTabToNewWindow(request.SourceID, *request.Dock,
                    request.LeafID, request.TabID, request.Specification);
            }
        }
        if (request.OnCompleted)
        {
            request.OnCompleted(result);
        }
    }
    // 補助Windowの入力Callbackから予約された復帰も、UI Frame終了後にだけ移譲します。
    std::vector<PendingUIAttach> attaches;
    attaches.swap(m_PendingUIAttaches);
    for (const PendingUIAttach& request : attaches)
    {
        UIContext* source = GetWindowUIContext(request.SourceID);
        if (source == nullptr || source->IsFrameActive() == true ||
            m_WindowManager.IsWindowClosePending(request.SourceID) == true)
        {
            continue;
        }
        for (const auto& child : source->GetRootElement().GetChildren())
        {
            if (child.get() != request.Child)
            {
                continue;
            }
            if (TransferUIRootChild(request.SourceID, m_MainWindowID, request.Child) == true)
            {
                // 新しいRootに所有権が移った後だけPointerを使用します。
                const math::Vec2 size = request.Child->GetSize();
                const math::Vec2 viewport = m_UIContext.GetViewportSize();
                // Main側でタイトルバーが掴める範囲を確保しつつ、Drop位置を維持します。
                request.Child->SetPosition(math::Vec2(
                    std::clamp(request.MainLocalDropPosition.x - size.x * 0.5f,
                        0.0f, std::max(0.0f, viewport.x - 48.0f)),
                    std::clamp(request.MainLocalDropPosition.y - 14.0f,
                        0.0f, std::max(0.0f, viewport.y - 28.0f))));
                BindUIWindowViewportTransfer(m_MainWindowID, *request.Child);
                // 他のRoot Childが残る補助Windowは閉じず、残ったUIの所有権を維持します。
                // CloseCleanupが残りのChildをMainへ移動してしまう副作用も避けます。
                // UIContextのRootには内部Popup Layerが常に1つ存在します。
                // 通常Childが残らず内部Layerだけになった場合に限りCloseします。
                if (source->GetRootElement().GetChildren().size() == 1u)
                {
                    m_WindowManager.RequestWindowClose(request.SourceID);
                }
            }
            break;
        }
    }
    for (PendingUIDetach& request : pending)
    {
        WindowID result = 0;
        UIContext* source = GetWindowUIContext(request.SourceID);
        if (source != nullptr && source->IsFrameActive() == false &&
            m_WindowManager.IsWindowClosePending(request.SourceID) == false)
        {
            // 予約から実行までにWidgetが削除されていてもdangling pointerを逆参照しません。
            for (const auto& item : source->GetRootElement().GetChildren())
            {
                if (item.get() == request.Child)
                {
                    result = DetachUIRootChildToNewWindow(
                        request.SourceID, request.Child, request.Specification);
                    break;
                }
            }
        }
        if (request.OnCompleted)
        {
            request.OnCompleted(result);
        }
    }
}

void Application::CompleteReleasedUIWindowDrags()
{
    if (m_RavenUIEnabled == false)
    {
        return;
    }
    // GLFWではWindow外でMouse Upが配送されない環境があります。
    // 通常EventでCaptureが解除されていれば何もせず、残留したUIWindow操作だけを補完します。
    const auto complete = [this](WindowID id, UIContext& ui)
    {
        UIWindow* logicalWindow = dynamic_cast<UIWindow*>(ui.GetMouseCaptureElement());
        if (logicalWindow == nullptr ||
            (logicalWindow->IsMoving() == false && logicalWindow->IsResizing() == false))
        {
            return;
        }
        Window* window = m_WindowManager.GetWindow(id);
        if (window == nullptr || m_WindowManager.IsWindowClosePending(id) == true)
        {
            return;
        }
        GLFWwindow* native = static_cast<GLFWwindow*>(window->GetNativeWindow());
        if (native == nullptr || glfwGetMouseButton(native, GLFW_MOUSE_BUTTON_LEFT) != GLFW_RELEASE)
        {
            return;
        }
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(native, &x, &y);
        // Widgetへ通常のUpを配送し、Capture解除と移譲要求を一つの経路に統一します。
        ui.RouteMouseUp(math::Vec2(static_cast<float>(x), static_cast<float>(y)),
            UIMouseButton::Left);
    };
    complete(m_MainWindowID, m_UIContext);
    for (const auto& entry : m_AuxiliaryUIContexts)
    {
        complete(entry.first, *entry.second);
    }
}

bool Application::BindUIWindowViewportTransfer(WindowID sourceID, UIWindow& window)
{
    UIContext* source = GetWindowUIContext(sourceID);
    if (source == nullptr || window.GetParent() != &source->GetRootElement())
    {
        return false;
    }

    // CallbackはUIWindowの入力配送中に呼ばれるため、Treeの所有権をその場で変更しません。
    // 既存のRequestDetachUIRootChildToNewWindowが重複予約と生存確認を担当します。
    window.SetOnViewportTransferRequested([this, sourceID](
        UIWindow* logicalWindow, const math::Vec2& releasePosition)
        {
            if (logicalWindow == nullptr)
            {
                return;
            }
            Window* sourceWindow = m_WindowManager.GetWindow(sourceID);
            if (sourceWindow == nullptr)
            {
                return;
            }
            const math::Vec2 size = logicalWindow->GetSize();
            WindowSpecification specification(
                logicalWindow->GetTitle().empty() == true
                    ? "Raven UI Window" : logicalWindow->GetTitle(),
                static_cast<unsigned int>(std::max(320.0f, size.x)),
                static_cast<unsigned int>(std::max(240.0f, size.y)),
                sourceWindow->GetBackend());
            // 新しいOS Window内では論理Windowの座標原点を戻し、
            // 元Viewportの画面座標を補助Windowへ持ち越さないようにします。
            if (sourceID != m_MainWindowID)
            {
                // GLFWのWindow座標はClient Area左上のScreen座標です。
                // 補助Windowの外なら無条件に戻すのではなく、MainのClient Areaへ
                // ドロップされた場合だけMain Rootへの移譲を予約します。
                GLFWwindow* sourceNative =
                    static_cast<GLFWwindow*>(sourceWindow->GetNativeWindow());
                GLFWwindow* mainNative =
                    static_cast<GLFWwindow*>(m_Window->GetNativeWindow());
                if (sourceNative == nullptr || mainNative == nullptr)
                {
                    return;
                }
                int sourceX = 0;
                int sourceY = 0;
                int mainX = 0;
                int mainY = 0;
                glfwGetWindowPos(sourceNative, &sourceX, &sourceY);
                glfwGetWindowPos(mainNative, &mainX, &mainY);
                const math::Vec2 mainLocalDrop(
                    static_cast<float>(sourceX - mainX) + releasePosition.x,
                    static_cast<float>(sourceY - mainY) + releasePosition.y);
                if (mainLocalDrop.x >= 0.0f && mainLocalDrop.y >= 0.0f &&
                    mainLocalDrop.x < static_cast<float>(m_Window->GetWidth()) &&
                    mainLocalDrop.y < static_cast<float>(m_Window->GetHeight()))
                {
                    RequestAttachUIWindowToMain(sourceID, logicalWindow, mainLocalDrop);
                }
                return;
            }
            RequestDetachUIRootChildToNewWindow(
                sourceID, logicalWindow, specification,
                [this, logicalWindow](WindowID destinationID)
                {
                    // 失敗時には予約後にWidgetが削除されている可能性があるため、
                    // Pointerを逆参照しません。成功時だけ移譲先Rootの生存確認を行います。
                    UIContext* destination = GetWindowUIContext(destinationID);
                    if (destination == nullptr)
                    {
                        return;
                    }
                    for (const auto& child : destination->GetRootElement().GetChildren())
                    {
                        if (child.get() == logicalWindow)
                        {
                            logicalWindow->SetPosition(math::Vec2(0.0f, 0.0f));
                            // 移譲後は補助Window IDで再Bindし、Mainへの復帰要求を受け付けます。
                            BindUIWindowViewportTransfer(destinationID, *logicalWindow);
                            break;
                        }
                    }
                });
        });
    return true;
}

bool Application::RequestAttachUIWindowToMain(WindowID sourceID, UIWindow* window,
    const math::Vec2& mainLocalDropPosition)
{
    UIContext* source = GetWindowUIContext(sourceID);
    if (sourceID == m_MainWindowID || source == nullptr || window == nullptr ||
        m_WindowManager.IsWindowClosePending(sourceID) == true)
    {
        return false;
    }
    bool found = false;
    for (const auto& child : source->GetRootElement().GetChildren())
    {
        if (child.get() == window)
        {
            found = true;
            break;
        }
    }
    if (found == false)
    {
        return false;
    }
    for (const PendingUIAttach& pending : m_PendingUIAttaches)
    {
        if (pending.SourceID == sourceID && pending.Child == window)
        {
            return false;
        }
    }
    m_PendingUIAttaches.push_back(PendingUIAttach{ sourceID, window, mainLocalDropPosition });
    return true;
}

UIContext* Application::GetWindowUIContext(WindowID id)
{
    if (id == m_MainWindowID)
    {
        return &m_UIContext;
    }
    const auto it = m_AuxiliaryUIContexts.find(id);
    return it != m_AuxiliaryUIContexts.end() ? it->second.get() : nullptr;
}

bool Application::TransferUIRootChild(
    WindowID sourceID, WindowID destinationID, UIElement* child)
{
    if (m_RavenUIEnabled == false || sourceID == destinationID || child == nullptr ||
        m_WindowManager.IsWindowClosePending(sourceID) == true ||
        m_WindowManager.IsWindowClosePending(destinationID) == true)
    {
        return false;
    }
    UIContext* source = GetWindowUIContext(sourceID);
    UIContext* destination = GetWindowUIContext(destinationID);
    if (source == nullptr || destination == nullptr)
    {
        return false;
    }
    return source->TransferRootChildTo(*destination, child);
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
    // 起動時などFrame処理外の即時切り替えはSceneManagerへ委譲します。
    m_SceneManager.SetScene(std::move(scene));
}

void Application::RequestSceneChange(Scope<Scene> scene)
{
    // Update / Event / UI callbackから現在Sceneを直接破棄しないよう、
    // 所有権だけを予約し、Present完了後の安全なFrame境界で反映します。
    m_SceneManager.RequestSceneChange(std::move(scene));
}

bool Application::RequestSceneTransition(
    Scope<Scene> scene, const SceneTransitionSpecification& specification)
{
    return m_SceneTransitionController.RequestTransition(std::move(scene), specification);
}

bool Application::RegisterScene(const std::string& sceneID, SceneFactoryFunction factory)
{
    return m_SceneFactory.Register(sceneID, std::move(factory));
}

bool Application::RequestSceneChange(const std::string& sceneID)
{
    Scope<Scene> scene = m_SceneFactory.Create(sceneID);
    if (scene == nullptr)
    {
        return false;
    }

    RequestSceneChange(std::move(scene));
    return true;
}

bool Application::RequestSceneTransition(
    const std::string& sceneID, const SceneTransitionSpecification& specification)
{
    Scope<Scene> scene = m_SceneFactory.Create(sceneID);
    if (scene == nullptr)
    {
        return false;
    }

    return RequestSceneTransition(std::move(scene), specification);
}

bool Application::RequestAsyncSceneTransition(
    const std::string& sceneID,
    SceneAsyncPreparation preparation,
    const SceneTransitionSpecification& specification)
{
    if (m_SceneFactory.Contains(sceneID) == false || preparation == nullptr)
    {
        return false;
    }

    // SceneFactory::Create()はWorkerへ渡しません。
    // Renderer / Physics / ECSを触る可能性があるScene constructorをApplication Threadに固定します。
    SceneCreationFunction sceneCreation = [this, sceneID]()
        {
            return m_SceneFactory.Create(sceneID);
        };

    return m_SceneTransitionController.RequestAsyncTransition(
        std::move(preparation), std::move(sceneCreation), specification);
}

RHIFrameResult Application::ExecuteExplicitSceneFrame(
    RHISceneFrameLifecycle& frame,
    const std::function<bool()>& prepare,
    const std::function<RHIFrameResult()>& drawPrepared)
{
    // Descriptorの更新はGPU Frame中に行わず、Acquire前に必ず完了させます。
    if (prepare == nullptr || drawPrepared == nullptr || prepare() == false)
    {
        return RHIFrameResult::FatalError;
    }
    const RHIFrameResult begin = frame.BeginFrame();
    if (begin != RHIFrameResult::Success)
    {
        // ResizeRequired時は描画せず、呼び出し元がSwapChainとPipelineを再生成します。
        return begin;
    }
    return drawPrepared();
}

bool Application::HandleExplicitSceneFrameResult(RHIFrameResult result,
    uint32_t width, uint32_t height, const ExplicitSceneCallbacks& callbacks)
{
    if (result == RHIFrameResult::Success)
    {
        return true;
    }

    // Prepare/Acquire/Drawが失敗した時点で、GPU Frameが参照する
    // Scene Snapshotを解放します。Resizeでも同じ順序を必須とします。
    if (callbacks.DiscardPrepared != nullptr)
    {
        callbacks.DiscardPrepared();
    }
    if (result == RHIFrameResult::ResizeRequired)
    {
        // Surfaceの変更は寸法が同じでも発生するため、強制再生成します。
        return callbacks.Resize != nullptr &&
            callbacks.Resize(width, height, true);
    }
    return false;
}

bool Application::HandleExplicitSceneResizeResult(bool resized,
    const ExplicitSceneCallbacks& callbacks)
{
    if (resized == true)
    {
        return true;
    }

    // Window通知によるResizeもAcquire失敗と同様にSnapshotを残しません。
    // Runtimeの部分的なSwapChain再生成失敗後は再描画せず所有元が終了します。
    if (callbacks.DiscardPrepared != nullptr)
    {
        callbacks.DiscardPrepared();
    }
    return false;
}

int Application::RunExplicitScene(Window& window, RHISceneFrameLifecycle& frame,
    const ExplicitSceneCallbacks& callbacks)
{
    GLFWwindow* native = static_cast<GLFWwindow*>(window.GetNativeWindow());
    if (native == nullptr || callbacks.OnScene == nullptr ||
        callbacks.Resize == nullptr || callbacks.Prepare == nullptr ||
        callbacks.DrawPrepared == nullptr || callbacks.DiscardPrepared == nullptr)
    {
        return 1;
    }

    // Explicit Sceneが所有するWindowからScene/LayerへEventを配送します。
    // 通常ApplicationのUI/Editor Event経路はこの独立Runnerへ持ち込みません。
    // WindowがRunnerより長生きする借用呼び出しでも、終了時にHookの参照を残しません。
    struct EventCallbackReset
    {
        Window& Target;
        ~EventCallbackReset()
        {
            Target.SetEventCallback([](Event&) {});
        }
    };
    const EventCallbackReset resetEventCallback{window};
    const auto onEvent = callbacks.OnEvent;
    window.SetEventCallback([onEvent](Event& event)
    {
        if (onEvent != nullptr)
        {
            onEvent(event);
        }
    });

    // OpenGL EditorのLayer/UI/Legacy CommandはExplicit Contextへ流さず、
    // Scene Queueと共通Frame境界だけを使用します。
    uint32_t previousWidth = 0;
    uint32_t previousHeight = 0;
    double previousTime = glfwGetTime();
    while (glfwWindowShouldClose(native) == GLFW_FALSE)
    {
        window.PollEvents();
        // Close通知を受けたFrameではScene更新やGPU Frame開始を行いません。
        // WindowClose Eventの配送はPollEvents中に完了しています。
        if (glfwWindowShouldClose(native) == GLFW_TRUE)
        {
            break;
        }
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(native, &width, &height);
        if (width <= 0 || height <= 0)
        {
            glfwWaitEvents();
            // 待機中にCloseされても次のLoopで描画せず終了します。
            // 最小化・復帰中の待機時間をSceneの更新dtへ加算しません。
            previousTime = glfwGetTime();
            continue;
        }
        const uint32_t targetWidth = static_cast<uint32_t>(width);
        const uint32_t targetHeight = static_cast<uint32_t>(height);
        if (previousWidth != targetWidth || previousHeight != targetHeight)
        {
            if (HandleExplicitSceneResizeResult(
                callbacks.Resize(targetWidth, targetHeight, false), callbacks) == false)
            {
                return 1;
            }
            previousWidth = targetWidth;
            previousHeight = targetHeight;
        }

        const double currentTime = glfwGetTime();
        // 通常Applicationと同じ上限を適用し、Debugger停止やWindow移動後の
        // 大きなdtがAnimation/Physicsへ一度に流れ込むことを防ぎます。
        const float frameDeltaTime = CalculateFrameDeltaTime(currentTime, previousTime);

        Renderer::BeginFrame();
        if (callbacks.OnUpdate != nullptr)
        {
            callbacks.OnUpdate(frameDeltaTime);
        }
        callbacks.OnScene();
        const RHIFrameResult result = ExecuteExplicitSceneFrame(
            frame, callbacks.Prepare, callbacks.DrawPrepared);
        if (HandleExplicitSceneFrameResult(
            result, targetWidth, targetHeight, callbacks) == false)
        {
            return 1;
        }
    }
    return 0;
}

void Application::Run()
{
    // Frame境界はConstructorでWindowと共に確定済みです。
    // 初期化失敗時にはScene / Layerを実行しません。
    if (m_Running == false || m_SceneFrame == nullptr)
    {
        m_Running = false;
        return;
    }
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
        // 通常/Explicitの両経路で同じdt上限と巻き戻り保護を適用します。
        const float frameDeltaTime = CalculateFrameDeltaTime(currentTime, previousTime);

        // Scene Transitionの時間はScene Updateより前に進めます。
        // FadeOut完了時もSceneManagerへ予約するだけなので、現在FrameのScene寿命は維持されます。
        m_SceneTransitionController.Update(frameDeltaTime);

        // ====================================================================
        // Renderer statistics frame boundary
        // ====================================================================
        // Renderer統計はApplication frame単位で集計します。
        // CPUProfilerもRenderer::BeginFrame()と同じ境界で、次frame開始時に直前frameを確定します。
        // Scene描画より前にResetすることで、Scene本体だけでなくPhysics / Animation Debug Overlayや
        // 後続Layerが発行した描画命令も同じframeのStatisticsとして集計できます。
        if (m_SceneFrame->BeginFrame() != RHIFrameResult::Success)
        {
            m_Running = false;
            break;
        }
        Renderer::BeginFrame();

        // ====================================================================
        // Raven UI frame begin
        // ====================================================================
        // DrawListはRetained UI Treeとは別に毎frame再構築します。
        // BeginFrameをScene / Layer処理より前に置くことで、Runtime LayerとEditor Layerのどちらからも
        // GetUIContext().GetDrawList()へ描画要求を追加できる共通frame境界になります。
        // 現在はUIContextがRoot UIElementを所有しているため、通常WidgetはDrawListへ直接書かず、
        // Root以下のRetained Treeを更新します。EndFrame()時にTreeからDrawListへ自動展開されます。
        if (m_RavenUIEnabled == true)
        {
            // Immediate宣言はUIContext::BeginFrameより前に完了させます。
            // SceneGame以外へ切り替わったFrameは空宣言で旧Panelを掃除します。
            if (m_ImmediateUI.BeginFrame() == true)
            {
                SceneGame* game = dynamic_cast<SceneGame*>(m_SceneManager.GetActiveScene());
                if (m_PhysicsDebugImmediatePanelEnabled == true && game != nullptr)
                {
                    ph::DrawPhysicsDebugImmediatePanel(m_ImmediateUI,
                        game->GetPhysicsDebugSettings(), m_PhysicsDebugImmediateFont,
                        math::Vec2(24.0f, 300.0f));
                }
                if (m_ImmediateUI.EndFrame() == false)
                {
                    m_ImmediateUI.AbortFrame();
                }
            }
        }

        if (m_RavenUIEnabled == true)
        {
            // GLFWのContent ScaleはWindowごとに変化します。毎frame同期することで
            // Resizeを伴わないMonitor移動も取りこぼさず、既存の論理座標は維持します。
            // Monitor移動時はDPI・Window論理サイズ・Framebuffer実Pixel数を同じFrameで同期します。
            // GLFWのMouse座標は論理座標のままUIContextへ配送します。
            m_UIContext.BeginFrame(math::Vec2(
                static_cast<float>(m_Window->GetWidth()),
                static_cast<float>(m_Window->GetHeight())),
                math::Vec2(static_cast<float>(m_Window->GetFramebufferWidth()),
                    static_cast<float>(m_Window->GetFramebufferHeight())),
                m_Window->GetContentScaleX(), m_Window->GetContentScaleY());
        }

        // ====================================================================
        // Runtime Scene
        // ====================================================================
        // Sceneはゲーム側のUpdate / Renderを担当します。
        // Editor処理はここへ混ぜず、後続のLayer更新へ分離します。
        Scene* activeScene = m_SceneManager.GetActiveScene();
        if (activeScene != nullptr)
        {
            activeScene->OnUpdate(frameDeltaTime);
            activeScene->OnRender();
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
        if (m_RavenUIEnabled == true)
        {
            // Scene Frame開始後かつUI Layout前の描画準備段階でFont Atlasをまとめて生成します。
            // DPI通知から直接GPU Textureを生成せず、同FrameのMeasure/Arrangeへ新Metricsを反映します。
            // UIが無効な場合はGPU生成・Tree走査とも実施しません。
            if (m_UIContext.GetPendingDPIFontCount() > 0u)
            {
                m_UIContext.RefreshPendingDPIFonts();
            }

            // Transition OverlayはUIContextのFrame Overlay Queueへ追加し、EndFrame内で
            // Retained Tree / Popup / Drag Previewより後へ合成します。GPU API固有処理はUIRendererへ委譲します。
            const float transitionAlpha = m_SceneTransitionController.GetOverlayAlpha();
            if (transitionAlpha > 0.0f)
            {
                const math::Vec2 viewportSize = m_UIContext.GetViewportSize();
                m_UIContext.AddFrameOverlayRect(
                    math::Vec2(0.0f, 0.0f),
                    viewportSize,
                    math::Vec4(0.0f, 0.0f, 0.0f, transitionAlpha));

                if (m_SceneTransitionController.IsLoading() == true)
                {
                    const float progress = m_SceneTransitionController.GetLoadingProgress();
                    const math::Vec2 center(viewportSize.x * 0.5f, viewportSize.y * 0.5f);

                    // Font Assetに依存しないLoading Indicatorです。
                    // 外周Circleと進捗Barだけで構成し、Scene/Font未初期化中でも描画可能にします。
                    const float spinnerRadius = 18.0f;
                    m_UIContext.AddFrameOverlayCircle(
                        math::Vec2(center.x - spinnerRadius, center.y - 42.0f - spinnerRadius),
                        math::Vec2(center.x + spinnerRadius, center.y - 42.0f + spinnerRadius),
                        math::Vec4(1.0f, 1.0f, 1.0f, 0.28f));

                    const float barWidth = std::min(320.0f, std::max(120.0f, viewportSize.x * 0.35f));
                    const float barHeight = 8.0f;
                    const math::Vec2 barMin(center.x - barWidth * 0.5f, center.y);
                    const math::Vec2 barMax(center.x + barWidth * 0.5f, center.y + barHeight);
                    m_UIContext.AddFrameOverlayRect(
                        barMin, barMax, math::Vec4(1.0f, 1.0f, 1.0f, 0.20f));

                    const float filledWidth = barWidth * std::clamp(progress, 0.0f, 1.0f);
                    if (filledWidth > 0.0f)
                    {
                        m_UIContext.AddFrameOverlayRect(
                            barMin,
                            math::Vec2(barMin.x + filledWidth, barMax.y),
                            math::Vec4(1.0f, 1.0f, 1.0f, 0.90f));
                    }
                }
            }
            m_UIContext.EndFrame();
        }

        // Layer更新中はMain UI FrameがActiveなのでTree移譲を行わず、ここで予約を処理します。
        // 補助Windowの描画反復前に生成を完了させ、unordered_mapの反復子無効化を防ぎます。
        FlushPendingUIDetaches();

        // 補助WindowのUIは専用GL Context/VAOとWindow別DPI・Framebufferで描画します。
        if (m_RavenUIEnabled == true)
        {
            for (const auto& item : m_AuxiliaryUIContexts)
            {
                UIContext* ui = item.second.get();
                m_WindowManager.RenderWindow(item.first, m_MainWindowID,
                    [ui](Window& window)
                    {
                        // 補助WindowはSceneのClearを通らないため、前Frameの残像を消します。
                        // UI Rendererが前Frameに残したScissorがClear範囲を狭めないよう無効化します。
                        glDisable(GL_SCISSOR_TEST);
                        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                        glClear(GL_COLOR_BUFFER_BIT);
                        ui->BeginFrame(
                            math::Vec2(static_cast<float>(window.GetWidth()),
                                static_cast<float>(window.GetHeight())),
                            math::Vec2(static_cast<float>(window.GetFramebufferWidth()),
                                static_cast<float>(window.GetFramebufferHeight())),
                            window.GetContentScaleX(), window.GetContentScaleY());
                        if (ui->GetPendingDPIFontCount() > 0u)
                        {
                            ui->RefreshPendingDPIFonts();
                        }
                        ui->EndFrame();
                    });
            }
        }

        // Scene / Layer / ImGui / Raven UIの全描画が完了した後にPresentします。
        // イベント処理とPresentを分離し、Clear DemoのFrame APIと同じ責務境界に揃えます。
        // 現時点のScene描画はOpenGLのみ。Vulkan/DX12のSwapChain Presentをここへ仮接続しません。
        if (m_SceneFrame->EndFrame() != RHIFrameResult::Success)
        {
            m_Running = false;
            break;
        }

        // GLFWのProcess共通Event Queueを一度処理し、補助WindowのCloseも安全に確定します。
        m_WindowManager.PollEvents();
        // Window外でMouse Upを取りこぼしても、次FrameへDrag/Captureを残しません。
        CompleteReleasedUIWindowDrags();
        if (m_SceneFrame->Present() != RHIFrameResult::Success)
        {
            m_Running = false;
            break;
        }

        // Scene / Layer / UIの更新・描画とPresentがすべて完了した後だけ、
        // Callback中に予約されたScene切り替えを反映します。
        // 旧Sceneを参照する一時的なFrame処理が完了してから破棄することで、
        // Update/Event/UI callback自身の実行中に所有元が消えることを防ぎます。
        m_SceneManager.FlushPendingSceneChange();
    }
}

void Application::OnAuxiliaryUIEvent(WindowID id, Event& event)
{
    UIContext* ui = GetWindowUIContext(id);
    if (ui == nullptr)
    {
        return;
    }
    // Main Windowと同じく、Focus移動前のIME所有者を記録してOSの未確定変換を同期します。
    UIInputText* imeOwner = dynamic_cast<UIInputText*>(ui->GetFocusedElement());
    const bool imeWasActive = imeOwner != nullptr &&
        imeOwner->GetIMEComposition().IsActive() == true;
    // 補助Windowの入力はMain WindowのLayer/UIContextへ転送しません。
    if (event.GetEventType() == EventType::WindowFocusLost)
    {
        // 補助Window間のドラッグでは、Focus喪失がMouse Upより先に届く場合があります。
        // 左ボタンが押されている論理Window操作だけCaptureを保持し、
        // ボタン解放はCompleteReleasedUIWindowDragsで補完します。
        UIWindow* capturedWindow = dynamic_cast<UIWindow*>(ui->GetMouseCaptureElement());
        Window* sourceWindow = m_WindowManager.GetWindow(id);
        GLFWwindow* native = sourceWindow != nullptr
            ? static_cast<GLFWwindow*>(sourceWindow->GetNativeWindow()) : nullptr;
        const bool keepWindowDrag = capturedWindow != nullptr &&
            (capturedWindow->IsMoving() == true || capturedWindow->IsResizing() == true) &&
            native != nullptr &&
            glfwGetMouseButton(native, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (keepWindowDrag == false)
        {
            ui->CancelMouseCapture();
        }
        ui->ClearFocus();
    }
    else if (event.GetEventType() == EventType::IMEComposition)
    {
        IMECompositionEvent& ime = static_cast<IMECompositionEvent&>(event);
        UIIMEEvent input;
        switch (ime.GetCompositionType())
        {
        case IMECompositionEventType::Begin: input.Type = UIIMEEventType::Begin; break;
        case IMECompositionEventType::Update: input.Type = UIIMEEventType::Update; break;
        case IMECompositionEventType::Commit: input.Type = UIIMEEventType::Commit; break;
        case IMECompositionEventType::End: input.Type = UIIMEEventType::End; break;
        case IMECompositionEventType::Cancel: input.Type = UIIMEEventType::Cancel; break;
        default: return;
        }
        input.Text = ime.GetText();
        input.Cursor = ime.GetCursor();
        input.SelectionStart = ime.GetSelectionStart();
        input.SelectionEnd = ime.GetSelectionEnd();
        event.Handled = ui->RouteIMEEvent(input);
    }
    else if (event.GetEventType() == EventType::KeyPressed ||
        event.GetEventType() == EventType::KeyReleased)
    {
        KeyEvent& key = static_cast<KeyEvent&>(event);
        UIKeyEvent input;
        input.Key = ToUIKey(key.GetKeyCode());
        input.Pressed = event.GetEventType() == EventType::KeyPressed;
        input.Shift = (key.GetModifiers() & GLFW_MOD_SHIFT) != 0;
        input.Control = (key.GetModifiers() & GLFW_MOD_CONTROL) != 0;
        input.Super = (key.GetModifiers() & GLFW_MOD_SUPER) != 0;
        input.Repeat = input.Pressed == true &&
            static_cast<KeyPressedEvent&>(event).IsRepeat();
        input.Context = ui;
        event.Handled = ui->RouteKeyEvent(input);
    }
    else if (event.GetEventType() == EventType::CharacterTyped)
    {
        event.Handled = ui->RouteCharacterEvent(
            static_cast<CharacterTypedEvent&>(event).GetCodepoint());
    }
    else if (event.GetEventType() == EventType::MouseMoved)
    {
        MouseMovedEvent& mouse = static_cast<MouseMovedEvent&>(event);
        event.Handled = ui->RouteMouseMove(math::Vec2(mouse.GetX(), mouse.GetY()));
    }
    else if (event.GetEventType() == EventType::MouseScrolled)
    {
        MouseScrolledEvent& mouse = static_cast<MouseScrolledEvent&>(event);
        event.Handled = ui->RouteMouseScroll(
            math::Vec2(mouse.GetX(), mouse.GetY()),
            math::Vec2(mouse.GetOffsetX(), mouse.GetOffsetY()));
    }
    else if (event.GetEventType() == EventType::MouseButtonPressed ||
        event.GetEventType() == EventType::MouseButtonReleased)
    {
        MouseButtonEvent& mouse = static_cast<MouseButtonEvent&>(event);
        UIMouseButton button = UIMouseButton::None;
        if (mouse.GetMouseButton() == GLFW_MOUSE_BUTTON_LEFT)
        {
            button = UIMouseButton::Left;
        }
        else if (mouse.GetMouseButton() == GLFW_MOUSE_BUTTON_RIGHT)
        {
            button = UIMouseButton::Right;
        }
        else if (mouse.GetMouseButton() == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            button = UIMouseButton::Middle;
        }
        if (button != UIMouseButton::None)
        {
            const math::Vec2 position(mouse.GetX(), mouse.GetY());
            event.Handled = event.GetEventType() == EventType::MouseButtonPressed
                ? ui->RouteMouseDown(position, button)
                : ui->RouteMouseUp(position, button);
        }
    }

    const bool imeEditingBoundary =
        event.GetEventType() == EventType::MouseButtonPressed ||
        (event.GetEventType() == EventType::KeyPressed &&
            static_cast<KeyPressedEvent&>(event).GetKeyCode() == GLFW_KEY_TAB);
    const UIInputText* currentIMEOwner =
        dynamic_cast<UIInputText*>(ui->GetFocusedElement());
    if (imeWasActive == true && imeEditingBoundary == true &&
        (currentIMEOwner != imeOwner ||
            (currentIMEOwner != nullptr &&
                currentIMEOwner->GetIMEComposition().IsActive() == false)))
    {
        Window* window = m_WindowManager.GetWindow(id);
        if (window != nullptr)
        {
            window->CancelIMEComposition();
        }
    }
}

void Application::OnEvent(Event& event)
{
    // 現段階ではEvent確認用ログを残しています。
    // Editor入力が増えてログ量が問題になった場合はDebug Logger側へ移行する想定です。
    std::cout << event.ToString() << std::endl;

    // UI側でFocus/編集位置が変わる前のIME所有者を記録します。
    // WindowからのCommit通知自体ではOSへ取消を返さず、Tab/Mouse Downだけを同期境界にします。
    UIInputText* imeOwner = m_RavenUIEnabled == true
        ? dynamic_cast<UIInputText*>(m_UIContext.GetFocusedElement()) : nullptr;
    const bool imeWasActive = imeOwner != nullptr &&
        imeOwner->GetIMEComposition().IsActive() == true;

    // WindowCloseはApplication自身が処理すべき最上位Eventです。
    // 処理済みにしてLayer側へ不要な伝播を行わないようにします。
    if (event.GetEventType() == EventType::WindowClose)
    {
        m_Running = false;
        event.Handled = true;
    }

    // WindowがFocusを失った場合、OS側でMouse Upが別Windowへ配送される可能性があります。
    // Capture所有WidgetへCancelを通知してからPressedも解除し、Slider/SplitterのDragging残留を防ぎます。
    // Focus Event自体はLayerも利用できるためHandledにはせず、後段へ通常通り伝播させます。
    if (m_RavenUIEnabled == true &&
        event.GetEventType() == EventType::WindowFocusLost)
    {
        m_UIContext.CancelMouseCapture();
        // OSのFocus喪失でも編集中の数値を確定し、再Focus時に途中入力を残しません。
        m_UIContext.ClearFocus();
    }

    // Fade / Scene交換待ち / FadeIn中は操作Eventをここで消費します。
    // Window lifecycle EventはResize・Close・Focus後処理に必要なためブロックしません。
    // Transition開始前から残っているCapture/Focusも解除し、Fade完了後にPressed状態等を持ち越しません。
    if (m_SceneTransitionController.BlocksInput() == true && event.Handled == false)
    {
        const EventType type = event.GetEventType();
        const bool inputEvent =
            type == EventType::KeyPressed ||
            type == EventType::KeyReleased ||
            type == EventType::CharacterTyped ||
            type == EventType::IMEComposition ||
            type == EventType::MouseMoved ||
            type == EventType::MouseButtonPressed ||
            type == EventType::MouseButtonReleased ||
            type == EventType::MouseScrolled;

        if (inputEvent == true)
        {
            if (m_RavenUIEnabled == true)
            {
                m_UIContext.CancelMouseCapture();
                m_UIContext.ClearFocus();
            }
            event.Handled = true;
            return;
        }
    }

    // ========================================================================
    // Raven UI Keyboard Event routing
    // ========================================================================
    // Platform Key CodeはApplication境界でSemantic UIKeyへ変換します。
    // ModifierもCore Eventが保持する入力時点のsnapshotを使い、後からInput pollingしません。
    if (m_RavenUIEnabled == true && event.Handled == false &&
        event.GetEventType() == EventType::KeyPressed)
    {
        KeyPressedEvent& keyEvent = static_cast<KeyPressedEvent&>(event);
        UIKeyEvent uiEvent;
        uiEvent.Key = ToUIKey(keyEvent.GetKeyCode());
        uiEvent.Pressed = true;
        uiEvent.Repeat = keyEvent.IsRepeat();
        uiEvent.Shift = (keyEvent.GetModifiers() & GLFW_MOD_SHIFT) != 0;
        uiEvent.Control = (keyEvent.GetModifiers() & GLFW_MOD_CONTROL) != 0;
        uiEvent.Super = (keyEvent.GetModifiers() & GLFW_MOD_SUPER) != 0;
        uiEvent.Context = &m_UIContext;
        event.Handled = m_UIContext.RouteKeyEvent(uiEvent);
    }
    else if (m_RavenUIEnabled == true && event.Handled == false &&
        event.GetEventType() == EventType::KeyReleased)
    {
        KeyReleasedEvent& keyEvent = static_cast<KeyReleasedEvent&>(event);
        UIKeyEvent uiEvent;
        uiEvent.Key = ToUIKey(keyEvent.GetKeyCode());
        uiEvent.Pressed = false;
        uiEvent.Shift = (keyEvent.GetModifiers() & GLFW_MOD_SHIFT) != 0;
        uiEvent.Context = &m_UIContext;
        event.Handled = m_UIContext.RouteKeyEvent(uiEvent);
    }

    if (m_RavenUIEnabled == true && event.Handled == false &&
        event.GetEventType() == EventType::CharacterTyped)
    {
        CharacterTypedEvent& characterEvent = static_cast<CharacterTypedEvent&>(event);
        event.Handled = m_UIContext.RouteCharacterEvent(characterEvent.GetCodepoint());
    }

    // CoreのIME通知をFocus所有Widgetへ橋渡しします。
    // UI無効時や未処理の場合はLayerへ従来どおり伝播します。
    if (m_RavenUIEnabled == true && event.Handled == false &&
        event.GetEventType() == EventType::IMEComposition)
    {
        IMECompositionEvent& imeEvent = static_cast<IMECompositionEvent&>(event);
        UIIMEEvent uiEvent;
        switch (imeEvent.GetCompositionType())
        {
        case IMECompositionEventType::Begin: uiEvent.Type = UIIMEEventType::Begin; break;
        case IMECompositionEventType::Update: uiEvent.Type = UIIMEEventType::Update; break;
        case IMECompositionEventType::Commit: uiEvent.Type = UIIMEEventType::Commit; break;
        case IMECompositionEventType::End: uiEvent.Type = UIIMEEventType::End; break;
        case IMECompositionEventType::Cancel: uiEvent.Type = UIIMEEventType::Cancel; break;
        default: break;
        }
        uiEvent.Text = imeEvent.GetText();
        uiEvent.Cursor = imeEvent.GetCursor();
        uiEvent.SelectionStart = imeEvent.GetSelectionStart();
        uiEvent.SelectionEnd = imeEvent.GetSelectionEnd();
        event.Handled = m_UIContext.RouteIMEEvent(uiEvent);
    }

    // ========================================================================
    // Raven UI Mouse Event routing
    // ========================================================================
    // Platform Mouse EventをUIContextのHit Test / Bubble Routingへ変換します。
    // Button / Scroll Eventも入力発生時の座標を自身に保持するため、ApplicationはInput pollingを行わず
    // Event snapshotだけからUI routingできます。これにより入力時刻と座標の対応を維持します。
    // UIEvent側でHandledになった場合だけCore EventもHandledとして、背後Layerへの入力漏れを防ぎます。
    if (m_RavenUIEnabled == true && event.Handled == false &&
        event.GetEventType() == EventType::MouseMoved)
    {
        MouseMovedEvent& mouseEvent = static_cast<MouseMovedEvent&>(event);
        event.Handled = m_UIContext.RouteMouseMove(math::Vec2(mouseEvent.GetX(), mouseEvent.GetY()));
    }
    else if (m_RavenUIEnabled == true && event.Handled == false &&
        event.GetEventType() == EventType::MouseScrolled)
    {
        MouseScrolledEvent& mouseEvent = static_cast<MouseScrolledEvent&>(event);
        event.Handled = m_UIContext.RouteMouseScroll(
            math::Vec2(mouseEvent.GetX(), mouseEvent.GetY()),
            math::Vec2(mouseEvent.GetOffsetX(), mouseEvent.GetOffsetY()));
    }
    else if (m_RavenUIEnabled == true && event.Handled == false &&
        event.GetEventType() == EventType::MouseButtonPressed)
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
    else if (m_RavenUIEnabled == true && event.Handled == false &&
        event.GetEventType() == EventType::MouseButtonReleased)
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

    // Focus移動・同じ入力欄でのCaret再配置はUIInputTextの未確定表示を破棄します。
    // OS側のIMEにも取消を依頼し、古い置換範囲へ後から確定文字が届くのを防ぎます。
    const bool imeEditingBoundary =
        event.GetEventType() == EventType::MouseButtonPressed ||
        (event.GetEventType() == EventType::KeyPressed &&
            static_cast<KeyPressedEvent&>(event).GetKeyCode() == GLFW_KEY_TAB);
    const UIInputText* currentIMEOwner = m_RavenUIEnabled == true
        ? dynamic_cast<UIInputText*>(m_UIContext.GetFocusedElement()) : nullptr;
    if (imeWasActive == true && imeEditingBoundary == true &&
        (currentIMEOwner != imeOwner ||
            (currentIMEOwner != nullptr &&
                currentIMEOwner->GetIMEComposition().IsActive() == false)))
    {
        m_Window->CancelIMEComposition();
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
        if (event.Handled == true)
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