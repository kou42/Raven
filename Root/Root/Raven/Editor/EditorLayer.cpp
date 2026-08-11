#include "Raven/Editor/EditorLayer.h"

#include "Raven/Core/Application.h"
#include "Raven/Scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>

namespace Raven
{

EditorLayer::EditorLayer(Application& application)
    : m_Application(&application)
{
}

void EditorLayer::OnAttach()
{
    // ========================================================================
    // Editor Viewport GPU resources
    // ========================================================================
    // Scene View / Game ViewはMain framebufferを直接見せるのではなく、
    // それぞれ専用FramebufferへSceneを描画し、そのColor Attachment Textureを
    // Dear ImGuiのWindow内へ表示します。
    //
    // 初期サイズはDock Layout確定前なので仮値です。
    // 実際の表示サイズはRenderSceneView()/RenderGameView()でContentRegionから取得し、
    // 次frameのOnRender()でFramebuffer::Resize()へ反映します。
    //
    // SceneとGameを最初から別Framebufferにしている理由は、次段階でScene Viewだけへ
    // Editor Camera / Grid / Selection Outline / Gizmo等を追加しても、Game Viewの
    // Runtime表示へEditor専用描画が混入しない構造を保つためです。
    m_SceneFramebuffer = std::make_unique<Framebuffer>(
        static_cast<std::uint32_t>(m_SceneViewportWidth),
        static_cast<std::uint32_t>(m_SceneViewportHeight));
    m_GameFramebuffer = std::make_unique<Framebuffer>(
        static_cast<std::uint32_t>(m_GameViewportWidth),
        static_cast<std::uint32_t>(m_GameViewportHeight));
}

void EditorLayer::OnDetach()
{
    // ========================================================================
    // Editor Viewport GPU resource shutdown
    // ========================================================================
    // Applicationの破棄順序により、この時点ではWindow / OpenGL Contextがまだ生存しています。
    // FramebufferのdestructorはOpenGLのFramebuffer / Texture / Renderbufferを削除するため、
    // Contextが破棄される前のEditorLayer::OnDetach()で明示的に解放します。
    m_SceneFramebuffer.reset();
    m_GameFramebuffer.reset();
}

void EditorLayer::OnUpdate(float dt)
{
    (void)dt;

    // ========================================================================
    // Editor-only update
    // ========================================================================
    // Runtime Sceneの更新はApplication -> Scene::OnUpdate()で行われます。
    // Editor CameraやGizmo等はゲームロジックとは独立したEditor状態なので、
    // 次段階ではこの入口から更新します。
    //
    // Framebuffer ResizeはGPU Resourceの再生成を伴うため、UI構築中ではなく
    // 描画処理側のOnRender()でまとめて行う方針です。
}

void EditorLayer::OnRender()
{
    if (m_Application == nullptr || m_Application->GetScene() == nullptr)
    {
        return;
    }

    // ========================================================================
    // Off-screen Scene / Game rendering
    // ========================================================================
    // Applicationは現在、互換性維持のためRuntime SceneをMain framebufferへ一度描画します。
    // その後EditorLayerからScene View / Game View専用Framebufferへ再描画しています。
    //
    // 現段階では両Viewとも既存SceneのRuntime Cameraを使用します。
    // 次段階でScene描画APIへCameraを外部指定できる経路を追加し、
    //   Scene View -> Editor Camera
    //   Game View  -> Runtime Camera
    // に分離します。
    //
    // 重要:
    // Scene/Gameを最初から別Framebufferへ描くことで、後からScene Viewだけに
    // Editor Camera / Grid / Gizmoを追加してもGame Viewへ混入しません。
    if (m_ShowSceneView && m_SceneFramebuffer != nullptr)
    {
        // ImGui Windowが極端に小さくなった場合でも0x0 Textureを作らないよう、
        // GPUへ渡すFramebufferサイズは最低1 pixelに丸めます。
        const std::uint32_t width = static_cast<std::uint32_t>(std::max(m_SceneViewportWidth, 1.0f));
        const std::uint32_t height = static_cast<std::uint32_t>(std::max(m_SceneViewportHeight, 1.0f));

        m_SceneFramebuffer->Resize(width, height);
        RenderSceneToFramebuffer(*m_SceneFramebuffer);
    }

    if (m_ShowGameView && m_GameFramebuffer != nullptr)
    {
        const std::uint32_t width = static_cast<std::uint32_t>(std::max(m_GameViewportWidth, 1.0f));
        const std::uint32_t height = static_cast<std::uint32_t>(std::max(m_GameViewportHeight, 1.0f));

        m_GameFramebuffer->Resize(width, height);
        RenderSceneToFramebuffer(*m_GameFramebuffer);
    }
}

void EditorLayer::RenderSceneToFramebuffer(Framebuffer& framebuffer)
{
    if (m_Application == nullptr)
    {
        return;
    }

    Scene* activeScene = m_Application->GetScene();
    if (activeScene == nullptr)
    {
        return;
    }

    // ========================================================================
    // Render target switching
    // ========================================================================
    // Framebuffer::Bind()はOpenGL FBOを切り替えるだけでなく、glViewportもAttachmentの
    // Width/Heightへ変更します。そのためScene::OnRender()内のClearを含む全描画命令は
    // Main framebufferではなく、このEditor Viewport用Framebufferへ出力されます。
    //
    // 描画終了後はFramebuffer::Unbind()でdefault framebufferへ戻します。
    // この復帰を忘れると、後続のImGui描画までoff-screen framebufferへ出力されるため重要です。
    framebuffer.Bind();
    activeScene->OnRender();
    framebuffer.Unbind();
}

void EditorLayer::OnImGuiRender(float dt)
{
    if (m_Application == nullptr)
    {
        return;
    }

    // ========================================================================
    // Editor selection validation
    // ========================================================================
    // Hierarchyが非表示でもRuntime側ではEntityが破棄されたりSceneが差し替わる可能性があります。
    // 選択状態はEditor全体で共有するため、Panel描画前に毎frame検証して無効参照を残しません。
    ValidateSelectedEntity();

    // ========================================================================
    // Editor root window / DockSpace
    // ========================================================================
    // 各Panelより先にDockSpaceを作ります。
    // Dear ImGui DockingではDock可能Windowの受け皿となるDockSpace Nodeが先に必要です。
    BeginDockSpace();

    // ========================================================================
    // Scene View / Game View
    // ========================================================================
    // OnRender()で作ったColor Attachment Textureを、独立したImGui Windowとして表示します。
    // WindowのContentRegionサイズは次frameのFramebuffer Resizeにも利用します。
    if (m_ShowSceneView)
    {
        RenderSceneView();
    }

    if (m_ShowGameView)
    {
        RenderGameView();
    }

    // ========================================================================
    // Statistics
    // ========================================================================
    // EditorLayerはPanelとApplicationの橋渡しだけを行います。
    // SceneをPanel内へ永続保持しないため、SetScene()後もそのframeのActive Sceneへ追従します。
    if (m_ShowStatisticsPanel)
    {
        m_StatisticsPanel.OnImGuiRender(
            dt,
            m_Application->GetWindow(),
            m_Application->GetScene());
    }

    // ========================================================================
    // Animation Debug
    // ========================================================================
    // AnimationDebugPanelもSceneを所有せず、毎frame現在のActive Sceneを受け取ります。
    // これによりScene差し替え時に古いSceneへの参照を保持しません。
    if (m_ShowAnimationDebugPanel)
    {
        m_AnimationDebugPanel.OnImGuiRender(m_Application->GetScene());
    }

    // ========================================================================
    // Scene Hierarchy
    // ========================================================================
    // HierarchyはActive SceneのEntity一覧を表示し、EditorLayerが所有する選択Entityだけを更新します。
    // 選択状態をPanelの外へ置くことで、Inspector / Scene View / Gizmoが同じEntityを利用できます。
    if (m_ShowSceneHierarchyPanel)
    {
        m_SceneHierarchyPanel.OnImGuiRender(
            m_Application->GetScene(),
            m_SelectedEntity);
    }

    // ========================================================================
    // Inspector
    // ========================================================================
    // Hierarchyが更新したm_SelectedEntityをそのまま渡します。
    // Inspector側に別の選択状態を作らないため、Hierarchyで選択した同じframeから
    // Component内容が表示され、将来のScene View / Gizmoとも選択対象が一致します。
    if (m_ShowInspectorPanel)
    {
        m_InspectorPanel.OnImGuiRender(m_SelectedEntity);
    }

    EndDockSpace();
}

void EditorLayer::RenderSceneView()
{
    // ========================================================================
    // Scene View Window
    // ========================================================================
    // WindowPaddingを0にすることで、Framebuffer TextureをContentRegionいっぱいに表示します。
    // PushStyleVar()/PopStyleVar()をこのWindowだけに限定し、他PanelのPaddingへ影響させません。
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("Scene View", &m_ShowSceneView);
    ImGui::PopStyleVar();

    if (visible)
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();

        if (available.x > 0.0f && available.y > 0.0f)
        {
            // ImGui WindowのContentRegionを次frameのFramebufferサイズとして保存します。
            // UI frame中にTextureを作り直さず、OnRender()でResizeすることでGPU Resource操作と
            // UI構築処理の責務を分離しています。
            m_SceneViewportWidth = available.x;
            m_SceneViewportHeight = available.y;

            if (m_SceneFramebuffer != nullptr)
            {
                // Ravenが固定しているDear ImGuiは1.92系です。
                // 1.91.4以降のdefault ImTextureIDはImU64なので、OpenGL GLuintを整数として
                // ImTextureIDへ明示変換して渡します。
                //
                // OpenGL Textureは左下原点ですがDear ImGuiのImageは通常左上から表示するため、
                // UV0=(0,1), UV1=(1,0)としてY方向を反転し、画面を正立させます。
                const ImTextureID textureID = static_cast<ImTextureID>(
                    m_SceneFramebuffer->GetColorAttachmentRendererID());

                ImGui::Image(
                    textureID,
                    available,
                    ImVec2(0.0f, 1.0f),
                    ImVec2(1.0f, 0.0f));
            }
        }
    }

    // ImGui::Begin()の戻り値に関係なく、Beginを呼んだ場合は必ずEndが必要です。
    ImGui::End();
}

void EditorLayer::RenderGameView()
{
    // ========================================================================
    // Game View Window
    // ========================================================================
    // Scene Viewと同じTexture表示経路を持ちますがFramebufferは共有しません。
    // 将来Scene ViewへEditor専用描画を追加した際にも、Game Viewは純粋なRuntime表示を保ちます。
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("Game View", &m_ShowGameView);
    ImGui::PopStyleVar();

    if (visible)
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();

        if (available.x > 0.0f && available.y > 0.0f)
        {
            m_GameViewportWidth = available.x;
            m_GameViewportHeight = available.y;

            if (m_GameFramebuffer != nullptr)
            {
                // Scene Viewと同じ理由でOpenGL TextureのY方向を反転して表示します。
                const ImTextureID textureID = static_cast<ImTextureID>(
                    m_GameFramebuffer->GetColorAttachmentRendererID());

                ImGui::Image(
                    textureID,
                    available,
                    ImVec2(0.0f, 1.0f),
                    ImVec2(1.0f, 0.0f));
            }
        }
    }

    ImGui::End();
}

void EditorLayer::OnEvent(Event& event)
{
    (void)event;

    // ========================================================================
    // Editor input routing
    // ========================================================================
    // Editor Camera / Gizmo入力は次段階でここへ追加します。
    // Scene Viewのhover/focus状態を確認し、Editor操作中の入力がRuntime Sceneへ流れないよう
    // 必要に応じてevent.Handledを設定する構造にします。
}

void EditorLayer::ValidateSelectedEntity()
{
    if (m_Application == nullptr)
    {
        m_SelectedEntity = Entity{};
        return;
    }

    Scene* activeScene = m_Application->GetScene();
    if (activeScene == nullptr)
    {
        m_SelectedEntity = Entity{};
        return;
    }

    // Entity::operator bool()はexplicitなので明示的にboolへ変換します。
    // Invalid Entityは「選択なし」として扱い、それ以上のScene/Generation検証は不要です。
    if (static_cast<bool>(m_SelectedEntity) == false)
    {
        return;
    }

    // Entity自身が保持するSceneと現在のActive Sceneを比較します。
    // Index/Generationが偶然一致しても別SceneのEntityは別物なので、Scene切替時に選択を解除します。
    if (m_SelectedEntity.GetScene() != activeScene)
    {
        m_SelectedEntity = Entity{};
        return;
    }

    // Generationまで含めた生存確認です。
    // Destroy後に同じIndexが再利用されても、古いGenerationを持つ選択Entityはここで無効化されます。
    if (activeScene->IsEntityAlive(m_SelectedEntity) == false)
    {
        m_SelectedEntity = Entity{};
    }
}

void EditorLayer::BeginDockSpace()
{
    // ========================================================================
    // Fullscreen DockSpace Host Window
    // ========================================================================
    // Main Viewport全体を覆うHost Windowを作り、その内部へEditor Panel用DockSpaceを配置します。
    // Host Window自身は通常のPanelではなく、MenuBarとDockSpaceを保持するRoot Containerです。
    //
    // 以前はRuntime SceneがMain framebufferへ直接表示されていたため、Host Windowに
    // NoBackground + PassthruCentralNodeを指定して背後のゲーム画面を透過表示していました。
    // 現在はScene/Game映像を専用Framebuffer TextureとしてImGui Windowへ表示するため、
    // その暫定透過処理は不要になっています。
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
    {
        return;
    }

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    // Host WindowはMain Viewportと一体化させるため、通常WindowのTitleBar / Resize / Move等を
    // 無効にします。DockされたScene ViewやInspector等は独立Windowなので通常どおり操作できます。
    ImGuiWindowFlags windowFlags = 0;
    windowFlags |= ImGuiWindowFlags_MenuBar;
    windowFlags |= ImGuiWindowFlags_NoDocking;
    windowFlags |= ImGuiWindowFlags_NoTitleBar;
    windowFlags |= ImGuiWindowFlags_NoCollapse;
    windowFlags |= ImGuiWindowFlags_NoResize;
    windowFlags |= ImGuiWindowFlags_NoMove;
    windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    windowFlags |= ImGuiWindowFlags_NoNavFocus;

    // Main Viewportを余白なしでDock領域として利用します。
    // Style変更はHost WindowのBegin()までに限定し、他Panelへ漏れないよう直後にPopします。
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("RavenEditorDockSpaceHost", nullptr, windowFlags);
    m_DockSpaceBegun = true;

    ImGui::PopStyleVar(3);

    // ========================================================================
    // DockSpace Node
    // ========================================================================
    // GetID()から安定したDockSpace IDを生成します。
    // ini保存が有効な場合、Dear ImGuiはこのIDを基準にDock Layoutを保存・復元します。
    const ImGuiID dockSpaceId = ImGui::GetID("RavenEditorDockSpace");

    // Scene/Game ViewがTexture Windowとして独立したためPassthruCentralNodeは使用しません。
    // DockSpace自身が通常背景を描画することで、Viewport Windowを閉じてもMain Sceneが
    // Host Windowの背後から意図せず透けて見えることを防ぎます。
    ImGui::DockSpace(
        dockSpaceId,
        ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_None);

    // Editor全体のMenuBarはHost Windowへ集約します。
    // 各Panel同士を依存させず、表示状態等のEditor全体操作をここから制御します。
    RenderMenuBar();
}

void EditorLayer::EndDockSpace()
{
    if (m_DockSpaceBegun == false)
    {
        return;
    }

    // ImGui::Begin()の戻り値に関係なくBeginを呼んだ場合は対応するEndが必要です。
    ImGui::End();
    m_DockSpaceBegun = false;
}

void EditorLayer::RenderMenuBar()
{
    if (ImGui::BeginMenuBar() == false)
    {
        return;
    }

    // ========================================================================
    // View menu
    // ========================================================================
    // Editor Windowの表示状態はEditorLayerで一元管理します。
    // Panel自身に他Panelの存在を知らせないことで、Panel間の不要な依存を作りません。
    if (ImGui::BeginMenu("View"))
    {
        // Scene/Game Viewも通常Panelと同じように表示状態を切り替えられます。
        ImGui::MenuItem("Scene View", nullptr, &m_ShowSceneView);
        ImGui::MenuItem("Game View", nullptr, &m_ShowGameView);
        ImGui::Separator();

        ImGui::MenuItem("Statistics", nullptr, &m_ShowStatisticsPanel);
        ImGui::MenuItem("Animation Debug", nullptr, &m_ShowAnimationDebugPanel);
        ImGui::MenuItem("Scene Hierarchy", nullptr, &m_ShowSceneHierarchyPanel);
        ImGui::MenuItem("Inspector", nullptr, &m_ShowInspectorPanel);
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

} // namespace Raven
