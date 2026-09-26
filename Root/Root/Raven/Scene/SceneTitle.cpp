#include "Raven/Scene/SceneTitle.h"

#include "Raven/Core/Application.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"

namespace Raven
{

void SceneTitle::OnCreate()
{
    // Title SceneはTransition経路の検証に必要な最小状態だけを持ちます。
    // Game用Asset/Physicsをここへ持ち込まず、Scene交換時の責務境界を明確にします。
    m_WasEnterPressed = Input::IsKeyPressed(Key::Enter);
}

void SceneTitle::OnDestroy()
{
    m_WasEnterPressed = false;
}

void SceneTitle::OnUpdateGame(float deltaTime)
{
    static_cast<void>(deltaTime);

    const bool enterPressed = Input::IsKeyPressed(Key::Enter);
    const bool transitionRequested = enterPressed == true && m_WasEnterPressed == false;
    m_WasEnterPressed = enterPressed;

    if (transitionRequested == false)
    {
        return;
    }

    SceneTransitionSpecification specification{};
    specification.Type = SceneTransitionType::Fade;
    specification.FadeOutDuration = 0.25f;
    specification.FadeInDuration = 0.25f;

    // Sceneを直接生成せずIDで要求し、Title側がSceneGame型へ依存しないようにします。
    m_Application.RequestSceneTransition("Game", specification);
}

void SceneTitle::OnRender()
{
    // 現段階ではTransition/lifetime検証用の空Sceneです。
    // Title UIはUIScreen / UINavigationManager導入時にRaven UI側へ追加します。
}

} // namespace Raven
