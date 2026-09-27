#include "Raven/Scene/SceneTitle.h"

#include "Raven/Core/Application.h"
#include "Raven/Assets/TextureAssetBatch.h"
#include "Raven/Scene/SceneGame.h"
#include "Raven/Scene/SceneGamePreparation.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Renderer.h"

#include <memory>
#include <vector>
#include "Raven/UI/Screens/TitleScreen.h"
#include "Raven/UI/Screens/SettingsScreen.h"

namespace Raven
{

void SceneTitle::OnCreate()
{
    // Scene切り替え時に前Scene由来のScreenを残さず、Title Scene専用Stackを構築します。
    UINavigationManager& navigation = m_Application.GetUINavigationManager();
    navigation.Clear();

    auto titleScreen = CreateScope<TitleScreen>(
        m_Application.GetRuntimeUIFont(),
        [this]()
        {
            SceneTransitionSpecification specification{};
            specification.Type = SceneTransitionType::Fade;
            specification.FadeOutDuration = 0.25f;
            specification.FadeInDuration = 0.25f;

            // Game Sceneで使用するSource TextureはFade暗転後にWorkerでCPU decodeします。
            // Batch自体はPreparation/Finalizeの両方から参照するためshared ownershipにします。
            auto textureBatch = std::make_shared<TextureAssetBatch>(
                std::vector<std::string>{ "Raven/Assets/Images/test/mountain1.png" });
            auto preparedResources = CreateRef<SceneGamePreparedResources>();

            m_Application.RequestAsyncSceneTransition(
                [textureBatch, preparedResources](SceneLoadingContext& context)
                {
                    const bool decoded = textureBatch->DecodeAll(
                        [&context](float progress)
                        {
                            // Texture decodeをPreparation全体の60%として扱います。
                            context.SetProgress(progress * 0.6f);
                        },
                        [&context]()
                        {
                            return context.IsCancellationRequested();
                        });
                    if (decoded == false || context.IsCancellationRequested() == true)
                    {
                        return false;
                    }

                    // Primitive頂点/Index生成はCPU処理だけなのでWorkerへ移します。
                    preparedResources->SphereGeometry =
                        PrimitiveMeshFactory::CreateSphereGeometry();
                    context.SetProgress(0.7f);
                    if (context.IsCancellationRequested() == true)
                    {
                        return false;
                    }

                    preparedResources->BoxGeometry =
                        PrimitiveMeshFactory::CreateCubeGeometry();
                    context.SetProgress(0.72f);
                    if (context.IsCancellationRequested() == true)
                    {
                        return false;
                    }

                    // FloorはStatic Grid、WaveはDynamic GridとしてCPU Geometryだけを先行生成します。
                    preparedResources->FloorGeometry =
                        PrimitiveMeshFactory::CreateDynamicGridGeometry(1, 1);
                    context.SetProgress(0.75f);
                    if (context.IsCancellationRequested() == true)
                    {
                        return false;
                    }

                    preparedResources->WaveGeometry =
                        PrimitiveMeshFactory::CreateDynamicGridGeometry(32, 32);
                    context.SetProgress(0.8f);
                    return preparedResources->IsValid();
                },
                [this, textureBatch]()
                {
                    // Legacy OpenGLではここでGPU Textureを生成します。
                    // Explicit Backendはdecode済みPixelを後段RHI Texture生成へ渡します。
                    const bool createRuntimeTextures = Renderer::IsExplicitSceneMode() == false;
                    const bool finalized = textureBatch->Finalize(
                        m_Application.GetTextureAssetManager(), createRuntimeTextures);
                    return finalized;
                },
                [this, preparedResources]()
                {
                    // Scene objectの生成とLayer構築はMain Threadで行います。
                    return CreateScope<SceneGame>(&m_Application, preparedResources);
                },
                specification);
        },
        [this]()
        {
            // Click処理中のTree Mutationを避け、Scene Update境界でPushします。
            m_SettingsRequested = true;
        },
        [this]()
        {
            m_Application.RequestExit();
        });

    navigation.PushScreen(std::move(titleScreen));
}

void SceneTitle::OnDestroy()
{
    m_SettingsRequested = false;
    m_SettingsBackRequested = false;

    // Title Scene固有UIをScene Lifetimeと同じ境界で破棄します。
    m_Application.GetUINavigationManager().Clear();
}

void SceneTitle::OnUpdateGame(float deltaTime)
{
    static_cast<void>(deltaTime);

    // UI callback実行中に、そのcallbackを所有するButton/Screenを破棄しないよう、
    // Navigation Treeの変更はScene Updateの安全な境界へ遅延します。
    if (m_SettingsBackRequested == true)
    {
        m_SettingsBackRequested = false;
        m_Application.GetUINavigationManager().PopScreen();
        return;
    }

    if (m_SettingsRequested == true)
    {
        m_SettingsRequested = false;

        auto settingsScreen = CreateScope<SettingsScreen>(
            m_Application.GetRuntimeUIFont(),
            [this]()
            {
                m_SettingsBackRequested = true;
            });
        m_Application.GetUINavigationManager().PushScreen(std::move(settingsScreen));
    }

    // Enter pollingによる直接遷移は廃止し、ButtonのMouse/Keyboard Actionへ統一しました。
}

void SceneTitle::OnRender()
{
    // Title UIはUIContext / UINavigationManager側で描画されます。
}

} // namespace Raven
