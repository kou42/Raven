#pragma once

#include "Raven/Scene/Scene.h"

namespace Raven
{

class Application;

// ============================================================================
// SceneTitle
// ============================================================================
// Scene Transitionの実利用確認用となる最小Runtime Sceneです。
// Game固有のPhysics/Assetを持たず、Enter入力から"Game" SceneへのFade遷移だけを要求します。
class SceneTitle final : public Scene
{
public:
    explicit SceneTitle(Application& application)
        : m_Application(application)
    {
    }

    void OnCreate() override;
    void OnDestroy() override;
    void OnUpdateGame(float deltaTime) override;
    void OnRender() override;

private:
    Application& m_Application;
    bool m_WasEnterPressed = false;
};

} // namespace Raven
