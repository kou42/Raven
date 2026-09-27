#include "Raven/Scene/SceneTitle.h"

#include "Raven/Core/Application.h"
#include "Raven/UI/Screens/TitleScreen.h"

namespace Raven
{

void SceneTitle::OnCreate()
{
    // Scene切り替え時に前Scene由来のScreenを残さず、Title Scene専用Stackを構築します。
    UINavigationManager& navigation = m_Application.GetUINavigationManager();
    navigation.Clear();

    auto titleScreen = CreateScope<TitleScreen>(
        [this]()
        {
            SceneTransitionSpecification specification{};
            specification.Type = SceneTransitionType::Fade;
            specification.FadeOutDuration = 0.25f;
            specification.FadeInDuration = 0.25f;

            // UI ActionはSceneを直接生成せず、従来どおりScene IDで遷移を要求します。
            m_Application.RequestSceneTransition("Game", specification);
        },
        []()
        {
            // SettingsScreen本体はPhase 9で追加します。
            // Action境界だけ先に確定し、TitleScreenへApplication/Scene依存を持ち込みません。
        },
        [this]()
        {
            m_Application.RequestExit();
        });

    navigation.PushScreen(std::move(titleScreen));
}

void SceneTitle::OnDestroy()
{
    // Title Scene固有UIをScene Lifetimeと同じ境界で破棄します。
    m_Application.GetUINavigationManager().Clear();
}

void SceneTitle::OnUpdateGame(float deltaTime)
{
    static_cast<void>(deltaTime);
    // Enter pollingによる直接遷移は廃止し、ButtonのMouse/Keyboard Actionへ統一しました。
}

void SceneTitle::OnRender()
{
    // Title UIはUIContext / UINavigationManager側で描画されます。
}

} // namespace Raven
