#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{

class SceneManager;

enum class SceneTransitionType
{
    Instant,
    Fade
};

struct SceneTransitionSpecification
{
    SceneTransitionType Type = SceneTransitionType::Instant;
    float FadeOutDuration = 0.25f;
    float FadeInDuration = 0.25f;
};

// Sceneの切り替えタイミングと画面Transitionの進行状態を管理します。
// SceneManagerはScene寿命だけを担当し、演出時間やOverlay Alphaを知りません。
class SceneTransitionController
{
public:
    explicit SceneTransitionController(SceneManager& sceneManager);

    // 遷移要求を開始します。同一遷移中の再要求は最後の要求で置き換えます。
    void RequestTransition(Scope<Scene> scene,
        const SceneTransitionSpecification& specification = {});

    // Application frameごとに呼び、Fadeの時間を進めます。
    void Update(float deltaTime);

    bool IsTransitioning() const;
    float GetOverlayAlpha() const { return m_OverlayAlpha; }

private:
    enum class State
    {
        Idle,
        FadeOut,
        WaitingForSceneChange,
        FadeIn
    };

    void RequestPendingSceneChange();
    static float NormalizeDuration(float duration);

private:
    SceneManager& m_SceneManager;
    Scope<Scene> m_TargetScene;
    SceneTransitionSpecification m_Specification{};
    State m_State = State::Idle;
    float m_ElapsedTime = 0.0f;
    float m_OverlayAlpha = 0.0f;
};

} // namespace Raven
