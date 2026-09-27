#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Scene/Scene.h"

#include <atomic>
#include <functional>
#include <future>
#include <memory>

namespace Raven
{

class SceneManager;
class JobSystem;

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

enum class SceneLoadError
{
    None,
    Cancelled,
    PreparationFailed,
    PreparationException,
    FinalizeFailed,
    FinalizeException,
    SceneCreationFailed,
    SceneCreationException
};

class SceneLoadingContext
{
public:
    void SetProgress(float progress);
    float GetProgress() const;

    bool IsCancellationRequested() const;
    void RequestCancellation();

private:
    std::atomic<float> m_Progress{ 0.0f };
    std::atomic<bool> m_CancellationRequested{ false };
};

using SceneAsyncPreparation = std::function<bool(SceneLoadingContext&)>;
using SceneMainThreadFinalize = std::function<bool()>;
using SceneCreationFunction = std::function<Scope<Scene>()>;

// Sceneの切り替えタイミングと画面Transitionの進行状態を管理します。
// SceneManagerはScene寿命だけを担当し、演出時間やOverlay Alphaを知りません。
class SceneTransitionController
{
public:
    SceneTransitionController(SceneManager& sceneManager, JobSystem& jobSystem);
    ~SceneTransitionController();

    // 遷移要求を開始します。進行中の再要求は状態競合を避けるため無視します。
    bool RequestTransition(Scope<Scene> scene,
        const SceneTransitionSpecification& specification = {});

    // FadeOut後の暗転中にCPU側の準備処理をWorker Threadで実行します。
    // Scene生成自体はPreparation完了後、必ずApplication ThreadのUpdate()内で実行します。
    bool RequestAsyncTransition(SceneAsyncPreparation preparation,
        SceneCreationFunction sceneCreation,
        const SceneTransitionSpecification& specification = {});

    // Worker Preparation後、Scene生成前にMain Thread専用のAsset登録/GPU finalizeを実行します。
    bool RequestAsyncTransition(SceneAsyncPreparation preparation,
        SceneMainThreadFinalize finalize,
        SceneCreationFunction sceneCreation,
        const SceneTransitionSpecification& specification = {});

    // Application frameごとに呼び、Fade / Loadingの状態を進めます。
    void Update(float deltaTime);

    bool IsTransitioning() const;
    bool IsLoading() const;
    bool DidLastAsyncLoadSucceed() const { return m_LastAsyncLoadSucceeded; }
    float GetLoadingProgress() const;
    SceneLoadError GetLastLoadError() const { return m_LastLoadError; }

    // Workerを強制停止せずCancellation Tokenを通知します。
    // Preparation側はIsCancellationRequested()を適切な粒度で確認して終了します。
    bool CancelAsyncTransition();
    float GetLoadingAnimationTime() const { return m_LoadingAnimationTime; }

    // Transition中はGame/UI操作を受け付けません。Window lifecycle EventはApplication側で別扱いします。
    bool BlocksInput() const { return IsTransitioning(); }
    float GetOverlayAlpha() const { return m_OverlayAlpha; }

private:
    enum class State
    {
        Idle,
        FadeOut,
        Loading,
        WaitingForSceneChange,
        FadeIn
    };

    void BeginAsyncLoading();
    void FinishWithoutSceneChange();
    void RequestPendingSceneChange();
    static float NormalizeDuration(float duration);

private:
    SceneManager& m_SceneManager;
    JobSystem& m_JobSystem;
    Scope<Scene> m_TargetScene;
    SceneTransitionSpecification m_Specification{};
    State m_State = State::Idle;
    float m_ElapsedTime = 0.0f;
    float m_OverlayAlpha = 0.0f;
    float m_LoadingAnimationTime = 0.0f;

    SceneAsyncPreparation m_AsyncPreparation;
    SceneMainThreadFinalize m_MainThreadFinalize;
    SceneCreationFunction m_AsyncSceneCreation;
    std::future<bool> m_AsyncPreparationFuture;
    bool m_AsyncRequested = false;
    bool m_LastAsyncLoadSucceeded = true;
    SceneLoadError m_LastLoadError = SceneLoadError::None;
    std::shared_ptr<SceneLoadingContext> m_LoadingContext;
};

} // namespace Raven
