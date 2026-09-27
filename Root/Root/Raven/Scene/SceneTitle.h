#pragma once

#include "Raven/Scene/Scene.h"

namespace Raven
{

class Application;

// ============================================================================
// SceneTitle
// ============================================================================
// Title Sceneはゲーム世界側のLifetimeだけを担当し、操作UIはTitleScreenへ委譲します。
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
    bool m_SettingsRequested = false;
    bool m_SettingsBackRequested = false;
};

} // namespace Raven
