#include "Raven/Editor/EditorLayer.h"

#include "Raven/Core/Application.h"
#include "Raven/Editor/EditorTranslateGizmo.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneViewportRenderer.h"

#include <imgui.h>
#include <imgui_internal.h>

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
    // 重要:
    // EditorLayerはOpenGLFramebuffer等の具体的なPlatform実装を直接生成しません。
    // Framebuffer::Create()を通すことで、現在のRendererAPIに対応した実装をRenderer層で選択し、
    // Editor側からOpenGL / DirectX / Vulkan等の違いを隠蔽します。
    //
    // 初期サイズはDock Layout確定前なので仮値です。
    // 実際の表示サイズはRenderSceneView()/RenderGameView()でContentRegionから取得し、
    // 次frameのOnRender()でFramebuffer::Resize()へ反映します。
    //
    // SceneとGameを最初から別Framebufferにしている理由は、Scene Viewだけへ
    // Editor Camera / Grid / Selection Outline / Gizmo等を追加しても、Game Viewの
    // Runtime表示へEditor専用描画が混入しない構造を保つためです。
    //
    // Scene ViewだけはEntity Picking用R32I Attachmentを追加します。
    // Game ViewはEditor選択情報を必要としないため、従来のRGBA8 + Depth構成を維持します。
    FramebufferSpecification sceneFramebufferSpecification;
    sceneFramebufferSpecification.Width =
        static_cast<std::uint32_t>(m_SceneViewportWidth);
    sceneFramebufferSpecification.Height =
        static_cast<std::uint32_t>(m_SceneViewportHeight);
    sceneFramebufferSpecification.Attachments =
    {
        TextureFormat::RGBA8,
        TextureFormat::R32I,
        TextureFormat::Depth24Stencil8
    };

    m_SceneFramebuffer = Framebuffer::Create(sceneFramebufferSpecification);

    m_GameFramebuffer = Framebuffer::Create(
        static_cast<std::uint32_t>(m_GameViewportWidth),
        static_cast<std::uint32_t>(m_GameViewportHeight));

    // EditorCameraのProjectionも初期Viewportサイズへ合わせます。
    // 以降はScene ViewのContentRegionサイズが変わるたびRenderSceneView()から更新します。
    m_EditorCamera.SetViewportSize(m_SceneViewportWidth, m_SceneViewportHeight);
}

void EditorLayer::OnDetach()
{
    // ========================================================================
    // Editor Viewport GPU resource shutdown
    // ========================================================================
    // Applicationの破棄順序により、この時点ではWindow / Graphics Contextがまだ生存しています。
    // Framebufferの具体実装はGPU Resourceを所有するため、Graphics Contextが破棄される前の
    // EditorLayer::OnDetach()で明示的に解放します。
    m_EntityPickingMaterial.reset();
    m_SelectionOutlineMaterial.reset();
    m_SceneFramebuffer.reset();
    m_GameFramebuffer.reset();
}

void EditorLayer::OnUpdate(float dt)
{
    // ========================================================================
    // Editor-only update
    // ========================================================================
    // Runtime Sceneの更新はApplication -> Scene::OnUpdate()で行われます。
    // Editor Camera / Gizmo等はゲームロジックとは独立したEditor状態なので、
    // Runtime更新へ混ぜずEditorLayerから更新します。
    //
    // ImGuiのhover/focus判定は前frameのRenderSceneView()で保存した値です。
    // OnUpdate()はImGui frame構築より先に呼ばれるため1frame遅れになりますが、
    // Scene View外でWASD/Mouse操作した時にEditor Cameraが動くことを防げます。
    const bool editorCameraInputEnabled =
        m_ShowSceneView
        && m_SceneViewportHovered
        && m_SceneViewportFocused;

    m_EditorCamera.Update(dt, editorCameraInputEnabled);

    // Framebuffer ResizeはGPU Resourceの再生成を伴うため、UI構築中ではなく
    // 描画処理側のOnRender()でまとめて行います。
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
    // ここからScene ViewとGame ViewのCameraを明確に分離します。
    //   Scene View -> EditorCamera
    //   Game View  -> Runtime Scene自身のCamera
    //
    // 同じScene状態を別Cameraから描くだけなので、Entity/Physics/AnimationのUpdateを
    // Scene View用に二重実行しないことが重要です。
    if (m_ShowSceneView && m_SceneFramebuffer != nullptr)
    {
        // ImGui Windowが極端に小さくなった場合でも0x0 Textureを作らないよう、
        // GPUへ渡すFramebufferサイズは最低1 pixelに丸めます。
        const std::uint32_t width = static_cast<std::uint32_t>(std::max(m_SceneViewportWidth, 1.0f));
        const std::uint32_t height = static_cast<std::uint32_t>(std::max(m_SceneViewportHeight, 1.0f));

        m_SceneFramebuffer->Resize(width, height);

        // EditorCameraはCamera基底を実装しているため、View/Projectionを個別に取り出さず
        // Cameraオブジェクトそのものを描画経路へ渡します。
        // これにより将来SceneCameraを追加しても同じ描画入口を再利用できます。
        RenderSceneToFramebuffer(*m_SceneFramebuffer, m_EditorCamera);
    }

    if (m_ShowGameView && m_GameFramebuffer != nullptr)
    {
        const std::uint32_t width = static_cast<std::uint32_t>(std::max(m_GameViewportWidth, 1.0f));
        const std::uint32_t height = static_cast<std::uint32_t>(std::max(m_GameViewportHeight, 1.0f));

        m_GameFramebuffer->Resize(width, height);

        // Game ViewはRuntime Cameraを変更せず、Scene本来のOnRender()をそのまま使います。
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
    // Runtime camera render target switching
    // ========================================================================
    // EditorLayerから見えるのはFramebuffer共通インターフェースだけです。
    // Bind()内部でOpenGLならFBO、将来DirectXなら対応するRender Targetを設定します。
    // 具体APIをEditorへ漏らさないことでRenderer backendの差し替え範囲を限定します。
    //
    // Scene::OnRender()内のClearを含む全描画命令は、Bindされたoff-screen描画先へ出力されます。
    // 描画終了後はUnbind()で通常の描画先へ戻し、後続のImGui描画へ影響を残しません。
    framebuffer.Bind();
    activeScene->OnRender();
    framebuffer.Unbind();
}

void EditorLayer::RenderSceneToFramebuffer(
    Framebuffer& framebuffer,
    const Camera& camera)
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

    framebuffer.Bind();

    // ========================================================================
    // External camera rendering
    // ========================================================================
    // Scene ViewではRuntime Cameraを上書きせず、SceneViewportRendererへCamera基底参照を
    // そのまま渡して一時的な別視点として描画します。
    // EditorLayer / SceneViewportRendererの境界でView/Projectionを分解しないため、
    // EditorCameraだけでなく今後追加するSceneCameraも同じ経路を利用できます。
    //
    // SceneViewportRendererを実装していないSceneでもEditorを完全に描画不能にしないため、
    // 通常のOnRender()へfallbackします。この場合Scene ViewはRuntime Camera表示になります。
    SceneViewportRenderer* viewportRenderer = dynamic_cast<SceneViewportRenderer*>(activeScene);
    if (viewportRenderer != nullptr)
    {
        viewportRenderer->RenderWithCamera(camera);
    }
    else
    {
        activeScene->OnRender();
    }

    // 通常Scene描画で確定したDepth Bufferを利用してPicking IDだけを別Passで書き込みます。
    // ゲーム用ShaderへEditor専用出力を追加しないため、描画責務を分離したままEntity選択を実現できます。
    RenderEntityPickingPass(framebuffer, camera);

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
    else
    {
        // Windowを非表示にしたframe以降、以前のhover/focus状態をCamera入力へ残しません。
        m_SceneViewportHovered = false;
        m_SceneViewportFocused = false;
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

    // ========================================================================
    // Scene View input state
    // ========================================================================
    // EditorCameraの入力はScene View上だけで有効にします。
    // Hoverだけでは別WindowがKeyboard focusを持つ場合があるため、Focusも併用して
    // Inspector編集中などにWASDがCameraへ入ることを避けます。
    m_SceneViewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    m_SceneViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

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

            // Projection行列はGPU Resourceではないため、ContentRegionを取得した時点で更新できます。
            // 次frameのOnRender()では最新Aspect RatioのEditor CameraからScene Viewを描画します。
            m_EditorCamera.SetViewportSize(
                m_SceneViewportWidth,
                m_SceneViewportHeight);

            if (m_SceneFramebuffer != nullptr)
            {
                // 現在のRendererAPIはOpenGLなのでRendererIDはOpenGL Texture IDとして扱えます。
                // この部分はFramebuffer抽象化の中でもまだbackend依存が残っている境界です。
                // DirectX/Vulkan対応時にはTexture/ImGui backendの抽象化と合わせて置き換えます。
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

                const ImVec2 imageMin = ImGui::GetItemRectMin();
                const ImVec2 imageMax = ImGui::GetItemRectMax();

                // ====================================================================
                // Translate Gizmo
                // ====================================================================
                // GizmoはFramebuffer Textureの上へDear ImGuiのDrawListで重ねます。
                // Editor専用UIなのでゲーム内UI/Rendererには依存させません。
                // 軸をクリックしたframeはScene Pickingを抑止し、Gizmo操作とEntity選択が競合しないようにします。
                const bool gizmoConsumedMouse = RenderTranslateGizmo(
                    m_SelectedEntity,
                    m_EditorCamera,
                    imageMin.x,
                    imageMin.y,
                    imageMax.x,
                    imageMax.y);

                // ====================================================================
                // Scene View Entity Picking
                // ====================================================================
                // Image Itemそのものが左クリックされた時だけPickingします。
                // Window全体のhover判定ではなくImage矩形を使うことで、将来Toolbar等を追加しても
                // UI上のクリックをScene選択として誤認しません。
                if (gizmoConsumedMouse == false
                    && ImGui::IsItemHovered()
                    && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    Scene* activeScene =
                        (m_Application != nullptr) ? m_Application->GetScene() : nullptr;

                    if (activeScene != nullptr
                        && m_SceneFramebuffer->GetColorAttachmentCount() > 1)
                    {
                        const ImVec2 mousePosition = ImGui::GetMousePos();

                        const float imageWidth = imageMax.x - imageMin.x;
                        const float imageHeight = imageMax.y - imageMin.y;

                        if (imageWidth > 0.0f && imageHeight > 0.0f)
                        {
                            const float localX = mousePosition.x - imageMin.x;
                            const float localY = mousePosition.y - imageMin.y;

                            const int framebufferWidth =
                                static_cast<int>(m_SceneFramebuffer->GetWidth());
                            const int framebufferHeight =
                                static_cast<int>(m_SceneFramebuffer->GetHeight());

                            if (framebufferWidth > 0 && framebufferHeight > 0)
                            {
                                // ImGui Imageは左上原点、OpenGL Framebufferは左下原点です。
                                // さらにWindow表示サイズとFramebuffer実サイズが1frameだけ異なる場合にも
                                // 対応できるよう、単純なpixel加算ではなく正規化して実サイズへ変換します。
                                int pixelX = static_cast<int>(
                                    (localX / imageWidth)
                                    * static_cast<float>(framebufferWidth));
                                int pixelY = static_cast<int>(
                                    (1.0f - (localY / imageHeight))
                                    * static_cast<float>(framebufferHeight));

                                pixelX = std::clamp(pixelX, 0, framebufferWidth - 1);
                                pixelY = std::clamp(pixelY, 0, framebufferHeight - 1);

                                const int entityID =
                                    m_SceneFramebuffer->ReadPixel(1, pixelX, pixelY);

                                if (entityID > 0)
                                {
                                    // Picking AttachmentへはEntityIndexだけを書き込みます。
                                    // GenerationはScene側の正規データから現在値を取得してEntityを再構築し、
                                    // Destroy/Index再利用後の古いHandleをEditor選択へ持ち込まないようにします。
                                    const EntityIndex entityIndex =
                                        static_cast<EntityIndex>(entityID);

                                    try
                                    {
                                        const EntityGeneration generation =
                                            activeScene->GetEntityGeneration(entityIndex);
                                        Entity pickedEntity(
                                            entityIndex,
                                            generation,
                                            activeScene);

                                        if (activeScene->IsEntityAlive(pickedEntity))
                                        {
                                            m_SelectedEntity = pickedEntity;
                                        }
                                        else
                                        {
                                            m_SelectedEntity = Entity{};
                                        }
                                    }
                                    catch (const std::out_of_range&)
                                    {
                                        // GPU Bufferへ古いIndexが残った場合でもEditorを落とさず選択解除します。
                                        // 通常はPicking Pass直後に読むため発生しませんが、安全な境界として扱います。
                                        m_SelectedEntity = Entity{};
                                    }
                                }
                                else
                                {
                                    // Geometryが存在しない背景をクリックした場合は選択解除します。
                                    m_SelectedEntity = Entity{};
                                }
                            }
                        }
                    }
                }
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
    // Scene ViewへEditor Camera / Gizmo等を追加しても、Game ViewはRuntime Cameraの
    // 純粋なゲーム表示を保ちます。
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
                // Scene Viewと同じ理由で、現在のOpenGL backendではTexture IDをImGuiへ渡し、
                // Y方向を反転して表示します。
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
    // 現在のEditorCameraはInput pollingで操作しています。
    // 将来Scroll Zoom / Gizmo / ShortcutなどEventベースの入力を追加する際には、
    // m_SceneViewportHovered / m_SceneViewportFocusedを確認し、Editor側で消費したEventへ
    // event.Handled = trueを設定してRuntime Layerへの伝播を止めます。
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

	/* 初期の画面レイアウトは以下のように想定しています。
    ┌─────────────────────────────────────────────────────┐
    │ Menu                                                                                                     │
    ├────────────┬─────────────────────────┬──────────────┤
    │                        │                                                  │                            │
    │  Scene                 │                  Scene View                      │         Inspector          │
    │ Hierarchy              │                  Game View[tab]                  │                            │
    │                        │                                                  │                            │
    ├────────────┴─────────────────────────┴──────────────┤
    │                          Raven Debug / Statistics | Animation Debug[tabs]                                │
    └─────────────────────────────────────────────────────┘
    */
    if (ImGui::DockBuilderGetNode(dockSpaceId) == nullptr)
    {
        ImGui::DockBuilderRemoveNode(dockSpaceId);
        ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceId, viewport->WorkSize);

        ImGuiID centerDockId = dockSpaceId;

        const ImGuiID leftDockId = ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Left,
            0.18f,
            nullptr,
            &centerDockId);

        const ImGuiID rightDockId = ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Right,
            0.22f,
            nullptr,
            &centerDockId);

        const ImGuiID bottomDockId = ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Down,
            0.25f,
            nullptr,
            &centerDockId);

        // Viewportは中央を最大限利用します。
        // 同じDock Nodeへ登録することでScene/Gameをタブ切り替えにします。
        ImGui::DockBuilderDockWindow("Scene View", centerDockId);
        ImGui::DockBuilderDockWindow("Game View", centerDockId);

        // Entity操作系は左右へ分離します。
        ImGui::DockBuilderDockWindow("Scene Hierarchy", leftDockId);
        ImGui::DockBuilderDockWindow("Inspector", rightDockId);

        // Debug系Windowは画面下部へまとめます。
        ImGui::DockBuilderDockWindow("Raven Debug / Statistics", bottomDockId);
        ImGui::DockBuilderDockWindow("Animation Debug", bottomDockId);

        ImGui::DockBuilderFinish(dockSpaceId);
    }

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
