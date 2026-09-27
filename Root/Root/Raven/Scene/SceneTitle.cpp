#include "Raven/Scene/SceneTitle.h"

#include "Raven/Core/Application.h"
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

            // UI ActionはSceneを直接生成せず、従来どおりScene IDで遷移を要求します。
            m_Application.RequestSceneTransition("Game", specification);
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
