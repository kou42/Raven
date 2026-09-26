#include "Raven/Scene/SceneTransitionController.h"

#include "Raven/Scene/SceneManager.h"

#include <algorithm>

namespace Raven
{

SceneTransitionController::SceneTransitionController(SceneManager& sceneManager)
    : m_SceneManager(sceneManager)
{
}

void SceneTransitionController::RequestTransition(
    Scope<Scene> scene, const SceneTransitionSpecification& specification)
{
    if (m_State != State::Idle)
    {
        return;
    }

    m_TargetScene = std::move(scene);
    m_Specification = specification;
    m_ElapsedTime = 0.0f;

    if (m_Specification.Type == SceneTransitionType::Instant)
    {
        m_OverlayAlpha = 0.0f;
        RequestPendingSceneChange();
        return;
    }

    const float fadeOutDuration = NormalizeDuration(m_Specification.FadeOutDuration);
    if (fadeOutDuration <= 0.0f)
    {
        // FadeOutが0秒でもScene交換はApplicationの安全なFrame境界まで遅延します。
        m_OverlayAlpha = 1.0f;
        RequestPendingSceneChange();
        return;
    }

    m_OverlayAlpha = 0.0f;
    m_State = State::FadeOut;
}

void SceneTransitionController::Update(float deltaTime)
{
    const float safeDeltaTime = std::max(deltaTime, 0.0f);

    if (m_State == State::FadeOut)
    {
        const float duration = NormalizeDuration(m_Specification.FadeOutDuration);
        m_ElapsedTime += safeDeltaTime;
        m_OverlayAlpha = duration > 0.0f
            ? std::clamp(m_ElapsedTime / duration, 0.0f, 1.0f) : 1.0f;

        if (m_OverlayAlpha >= 1.0f)
        {
            // 完全に暗転したFrameを描画してからSceneを交換します。
            // SceneManagerのDeferred Queueへ渡すため、この時点では旧Sceneは破棄されません。
            RequestPendingSceneChange();
        }
        return;
    }

    if (m_State == State::WaitingForSceneChange)
    {
        // Application末尾でSceneManagerのQueueがFlushされた次FrameからFadeInへ進みます。
        if (m_SceneManager.HasPendingSceneChange() == false)
        {
            m_ElapsedTime = 0.0f;
            if (m_Specification.Type == SceneTransitionType::Instant)
            {
                m_OverlayAlpha = 0.0f;
                m_State = State::Idle;
                return;
            }

            const float duration = NormalizeDuration(m_Specification.FadeInDuration);
            if (duration <= 0.0f)
            {
                m_OverlayAlpha = 0.0f;
                m_State = State::Idle;
            }
            else
            {
                m_State = State::FadeIn;
            }
        }
        return;
    }

    if (m_State == State::FadeIn)
    {
        const float duration = NormalizeDuration(m_Specification.FadeInDuration);
        m_ElapsedTime += safeDeltaTime;
        const float progress = duration > 0.0f
            ? std::clamp(m_ElapsedTime / duration, 0.0f, 1.0f) : 1.0f;
        m_OverlayAlpha = 1.0f - progress;

        if (progress >= 1.0f)
        {
            m_OverlayAlpha = 0.0f;
            m_State = State::Idle;
        }
    }
}

bool SceneTransitionController::IsTransitioning() const
{
    return m_State != State::Idle;
}

void SceneTransitionController::RequestPendingSceneChange()
{
    m_SceneManager.RequestSceneChange(std::move(m_TargetScene));
    m_State = State::WaitingForSceneChange;
    m_ElapsedTime = 0.0f;
}

float SceneTransitionController::NormalizeDuration(float duration)
{
    return std::max(duration, 0.0f);
}

} // namespace Raven
