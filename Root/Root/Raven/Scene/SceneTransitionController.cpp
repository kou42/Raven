#include "Raven/Scene/SceneTransitionController.h"

#include "Raven/Scene/SceneManager.h"

#include <algorithm>
#include <chrono>

namespace Raven
{

void SceneLoadingProgress::Set(float progress)
{
    m_Progress.store(std::clamp(progress, 0.0f, 1.0f), std::memory_order_relaxed);
}

float SceneLoadingProgress::Get() const
{
    return m_Progress.load(std::memory_order_relaxed);
}

SceneTransitionController::SceneTransitionController(SceneManager& sceneManager)
    : m_SceneManager(sceneManager)
{
}

SceneTransitionController::~SceneTransitionController()
{
    // std::asyncのWorkerがController内Callbackを参照したまま破棄されないよう完了を待ちます。
    if (m_AsyncPreparationFuture.valid() == true)
    {
        m_AsyncPreparationFuture.wait();
    }
}

bool SceneTransitionController::RequestTransition(
    Scope<Scene> scene, const SceneTransitionSpecification& specification)
{
    if (m_State != State::Idle)
    {
        return false;
    }

    m_TargetScene = std::move(scene);
    m_Specification = specification;
    m_ElapsedTime = 0.0f;
    m_LastAsyncLoadSucceeded = true;

    if (m_Specification.Type == SceneTransitionType::Instant)
    {
        m_OverlayAlpha = 0.0f;
        RequestPendingSceneChange();
        return true;
    }

    const float fadeOutDuration = NormalizeDuration(m_Specification.FadeOutDuration);
    if (fadeOutDuration <= 0.0f)
    {
        m_OverlayAlpha = 1.0f;
        RequestPendingSceneChange();
        return true;
    }

    m_OverlayAlpha = 0.0f;
    m_State = State::FadeOut;
    return true;
}

bool SceneTransitionController::RequestAsyncTransition(
    SceneAsyncPreparation preparation,
    SceneCreationFunction sceneCreation,
    const SceneTransitionSpecification& specification)
{
    if (m_State != State::Idle || preparation == nullptr || sceneCreation == nullptr)
    {
        return false;
    }

    m_Specification = specification;
    m_ElapsedTime = 0.0f;
    m_OverlayAlpha = specification.Type == SceneTransitionType::Instant ? 1.0f : 0.0f;
    m_AsyncPreparation = std::move(preparation);
    m_AsyncSceneCreation = std::move(sceneCreation);
    m_AsyncRequested = true;
    m_LastAsyncLoadSucceeded = true;
    m_LoadingProgress = std::make_shared<SceneLoadingProgress>();

    const float fadeOutDuration = NormalizeDuration(m_Specification.FadeOutDuration);
    if (m_Specification.Type == SceneTransitionType::Instant || fadeOutDuration <= 0.0f)
    {
        // Async遷移ではInstantでもLoading中の未完成Sceneを見せないため暗転します。
        m_OverlayAlpha = 1.0f;
        BeginAsyncLoading();
    }
    else
    {
        m_State = State::FadeOut;
    }

    return true;
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
            if (m_AsyncRequested == true)
            {
                BeginAsyncLoading();
            }
            else
            {
                RequestPendingSceneChange();
            }
        }
        return;
    }

    if (m_State == State::Loading)
    {
        m_LoadingAnimationTime += safeDeltaTime;

        // wait_for(0)だけで完了確認し、Application ThreadをLoading待ちで停止させません。
        if (m_AsyncPreparationFuture.valid() == true &&
            m_AsyncPreparationFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            bool prepared = false;
            try
            {
                prepared = m_AsyncPreparationFuture.get();
            }
            catch (...)
            {
                // Worker例外をApplication loop外へ伝播させません。
                // 失敗として扱い、現在Sceneを維持して暗転だけ解除します。
                prepared = false;
            }
            if (prepared == false)
            {
                m_LastAsyncLoadSucceeded = false;
                FinishWithoutSceneChange();
                return;
            }

            // Scene / Renderer / ECS初期化にはMain Thread制約を持つ処理が含まれ得ます。
            // WorkerではCPU側Preparationだけを行い、Scene生成は必ずこのUpdate()内で実行します。
            try
            {
                m_TargetScene = m_AsyncSceneCreation();
            }
            catch (...)
            {
                m_TargetScene.reset();
            }
            if (m_TargetScene == nullptr)
            {
                m_LastAsyncLoadSucceeded = false;
                FinishWithoutSceneChange();
                return;
            }

            m_LastAsyncLoadSucceeded = true;
            if (m_LoadingProgress != nullptr)
            {
                m_LoadingProgress->Set(1.0f);
            }
            RequestPendingSceneChange();
        }
        return;
    }

    if (m_State == State::WaitingForSceneChange)
    {
        if (m_SceneManager.HasPendingSceneChange() == false)
        {
            m_ElapsedTime = 0.0f;
            if (m_Specification.Type == SceneTransitionType::Instant && m_AsyncRequested == false)
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
                m_AsyncRequested = false;
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
            m_AsyncRequested = false;
            m_LoadingProgress.reset();
        }
    }
}

bool SceneTransitionController::IsTransitioning() const
{
    return m_State != State::Idle;
}

bool SceneTransitionController::IsLoading() const
{
    return m_State == State::Loading;
}

float SceneTransitionController::GetLoadingProgress() const
{
    if (m_LoadingProgress == nullptr)
    {
        return 0.0f;
    }

    return m_LoadingProgress->Get();
}

void SceneTransitionController::BeginAsyncLoading()
{
    m_ElapsedTime = 0.0f;
    m_OverlayAlpha = 1.0f;
    m_State = State::Loading;
    m_LoadingAnimationTime = 0.0f;

    SceneAsyncPreparation preparation = std::move(m_AsyncPreparation);
    const std::shared_ptr<SceneLoadingProgress> progress = m_LoadingProgress;
    m_AsyncPreparationFuture = std::async(std::launch::async,
        [preparation = std::move(preparation), progress]() mutable
        {
            const bool succeeded = preparation(*progress);
            if (succeeded == true)
            {
                progress->Set(1.0f);
            }
            return succeeded;
        });
}

void SceneTransitionController::FinishWithoutSceneChange()
{
    m_AsyncPreparation = {};
    m_AsyncSceneCreation = {};
    m_AsyncRequested = false;
    m_LoadingProgress.reset();
    m_ElapsedTime = 0.0f;

    const float duration = NormalizeDuration(m_Specification.FadeInDuration);
    if (duration <= 0.0f)
    {
        m_OverlayAlpha = 0.0f;
        m_State = State::Idle;
    }
    else
    {
        // Load失敗時は旧Sceneを維持したまま暗転を解除します。
        m_State = State::FadeIn;
    }
}

void SceneTransitionController::RequestPendingSceneChange()
{
    m_SceneManager.RequestSceneChange(std::move(m_TargetScene));
    m_AsyncPreparation = {};
    m_AsyncSceneCreation = {};
    m_State = State::WaitingForSceneChange;
    m_ElapsedTime = 0.0f;
}

float SceneTransitionController::NormalizeDuration(float duration)
{
    return std::max(duration, 0.0f);
}

} // namespace Raven
