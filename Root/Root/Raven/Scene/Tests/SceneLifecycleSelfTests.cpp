#include "Raven/Scene/Tests/SceneLifecycleSelfTests.h"

#include <cassert>
#include <type_traits>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Core/ApplicationState.h"
#include "Raven/Audio/AudioService.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneManager.h"
#include "Raven/Scene/SceneFactory.h"
#include "Raven/Scene/SceneStreamingController.h"

namespace Raven::tests
{
namespace
{

// ApplicationはScope<Scene>でSceneGame等の派生Sceneを所有します。
// 基底型経由のdeleteで派生デストラクタが確実に呼ばれることをcompile-timeで固定します。
static_assert(std::has_virtual_destructor_v<Scene>);

class CountingLayer final : public Layer
{
public:
    CountingLayer(
        int layerId,
        int& attachCount,
        int& updateCount,
        int& detachCount,
        std::vector<int>& attachOrder,
        std::vector<int>& detachOrder)
        : m_LayerId(layerId)
        , m_AttachCount(attachCount)
        , m_UpdateCount(updateCount)
        , m_DetachCount(detachCount)
        , m_AttachOrder(attachOrder)
        , m_DetachOrder(detachOrder)
    {
    }

    void OnAttach() override
    {
        ++m_AttachCount;
        m_AttachOrder.push_back(m_LayerId);
    }

    void OnDetach() override
    {
        ++m_DetachCount;
        m_DetachOrder.push_back(m_LayerId);
    }

    void OnUpdate(float deltaTime) override
    {
        static_cast<void>(deltaTime);
        ++m_UpdateCount;
    }

private:
    int m_LayerId = 0;
    int& m_AttachCount;
    int& m_UpdateCount;
    int& m_DetachCount;
    std::vector<int>& m_AttachOrder;
    std::vector<int>& m_DetachOrder;
};

class CountingScene final : public Scene
{
public:
    CountingScene(int sceneId, int& createCount, int& destroyCount)
        : m_SceneId(sceneId)
        , m_CreateCount(createCount)
        , m_DestroyCount(destroyCount)
    {
    }

    void OnCreate() override
    {
        ++m_CreateCount;
    }

    void OnDestroy() override
    {
        ++m_DestroyCount;
    }

    int GetSceneId() const { return m_SceneId; }

private:
    int m_SceneId = 0;
    int& m_CreateCount;
    int& m_DestroyCount;
};

class CountingAudioBackend final : public IAudioService
{
public:
    CountingAudioBackend(int& startCount, int& updateCount, int& stopCount)
        : m_StartCount(startCount)
        , m_UpdateCount(updateCount)
        , m_StopCount(stopCount)
    {
    }

    bool Start() override
    {
        ++m_StartCount;
        return true;
    }

    void Update(float deltaTime) override
    {
        assert(deltaTime >= 0.0f);
        ++m_UpdateCount;
    }

    void Stop() override
    {
        ++m_StopCount;
    }

private:
    int& m_StartCount;
    int& m_UpdateCount;
    int& m_StopCount;
};

} // namespace

void RunSceneLifecycleSelfTests()
{
    Scene scene{};

    // ========================================================================
    // 1. Scene内部LayerのAttach / Update契約
    // ========================================================================
    // Scene::PushLayer()がOnAttach()を担当し、毎フレームの更新入口は
    // Scene::OnUpdate() -> OnUpdateLayer()へ一本化します。
    // 派生SceneのOnUpdateGame()から同じLayerを直接更新すると二重Updateになるため、
    // 基底Sceneの契約をこのSelfTestで明示します。
    int attachCount = 0;
    int updateCount = 0;
    int detachCount = 0;
    std::vector<int> attachOrder;
    std::vector<int> detachOrder;

    scene.PushLayer(CreateScope<CountingLayer>(
        1,
        attachCount,
        updateCount,
        detachCount,
        attachOrder,
        detachOrder));
    scene.PushLayer(CreateScope<CountingLayer>(
        2,
        attachCount,
        updateCount,
        detachCount,
        attachOrder,
        detachOrder));

    assert(attachCount == 2);
    assert(updateCount == 0);
    assert(detachCount == 0);
    assert(attachOrder.size() == 2u);
    assert(attachOrder[0] == 1);
    assert(attachOrder[1] == 2);

    scene.OnUpdate(0.0f);
    assert(updateCount == 2);

    // ========================================================================
    // 2. Immediate / queued / leaked-style Entityを同時に用意する
    // ========================================================================
    // Scene終了処理は、生成者が通常経路で破棄したEntity・DestroyQueueへ積まれたEntity・
    // どこからも明示破棄されなかった残存Entityが混在していても安全である必要があります。
    Entity alreadyDestroyed = scene.CreateEntity("AlreadyDestroyed");
    Entity queuedDestroy = scene.CreateEntity("QueuedDestroy");
    Entity remaining = scene.CreateEntity("Remaining");

    scene.DestroyEntity(alreadyDestroyed);
    scene.QueueDestroyEntity(queuedDestroy);

    assert(scene.IsEntityAlive(alreadyDestroyed) == false);
    assert(scene.IsEntityAlive(queuedDestroy) == true);
    assert(scene.IsEntityAlive(remaining) == true);

    // ========================================================================
    // 3. Scene teardownはLayerを逆順DetachしてからEntityを最終回収する
    // ========================================================================
    // Layerは登録順とは逆のLIFO順でDetachします。
    // 後から積まれたOverlay等が先に積まれたLayerへ依存していても、依存先より先に終了しません。
    // Layerが所有EntityをOnDetach()で破棄できるよう、Entity最終Sweepより前にDetachすることも
    // Scene lifecycleの重要な契約です。
    scene.OnDestroy();

    assert(detachCount == 2);
    assert(detachOrder.size() == 2u);
    assert(detachOrder[0] == 2);
    assert(detachOrder[1] == 1);

    assert(scene.IsEntityAlive(alreadyDestroyed) == false);
    assert(scene.IsEntityAlive(queuedDestroy) == false);
    assert(scene.IsEntityAlive(remaining) == false);

    // ========================================================================
    // 4. OnDestroy()の多重呼び出し
    // ========================================================================
    // ApplicationのScene差し替え・終了順序が将来変更されても、teardown自体は冪等であるべきです。
    // Layer Containerは最初のOnDestroy()でclear済みなので、2回目にOnDetach()を再実行しません。
    // 既にAlive=falseのSlotも再破棄せず、安全に終了することを確認します。
    scene.OnDestroy();

    assert(detachCount == 2);
    assert(detachOrder.size() == 2u);
    assert(scene.IsEntityAlive(alreadyDestroyed) == false);
    assert(scene.IsEntityAlive(queuedDestroy) == false);
    assert(scene.IsEntityAlive(remaining) == false);

    // ========================================================================
    // 5. Persistent / Primary / Additiveの所有順とDeferred操作
    // ========================================================================
    // Persistent EntityをPrimaryへ移送するとHandle/Component Storageの所有先が曖昧になるため、
    // Persistent Scene自身を交換対象外として保持します。Additiveも固有IDで所有し、Load/Unloadを
    // Frame安全境界まで遅延することでUpdate/Event走査中の破棄を防ぎます。
    int persistentCreateCount = 0;
    int persistentDestroyCount = 0;
    int primaryCreateCount = 0;
    int primaryDestroyCount = 0;
    int replacementCreateCount = 0;
    int replacementDestroyCount = 0;
    int additiveCreateCount = 0;
    int additiveDestroyCount = 0;

    SceneManager manager;
    manager.SetPersistentScene(CreateScope<CountingScene>(
        10, persistentCreateCount, persistentDestroyCount));
    manager.SetScene(CreateScope<CountingScene>(
        20, primaryCreateCount, primaryDestroyCount));

    assert(persistentCreateCount == 1);
    assert(primaryCreateCount == 1);
    const SceneInstanceID persistentID = manager.GetPersistentSceneID();
    const SceneInstanceID primaryID = manager.GetActiveSceneID();
    assert(persistentID != InvalidSceneInstanceID);
    assert(primaryID != InvalidSceneInstanceID);
    assert(persistentID != primaryID);

    const SceneInstanceID additiveID = manager.LoadSceneAdditive(
        CreateScope<CountingScene>(30, additiveCreateCount, additiveDestroyCount));
    assert(additiveID != InvalidSceneInstanceID);
    assert(additiveCreateCount == 0);
    assert(manager.IsSceneLoaded(additiveID) == false);
    assert(manager.FlushPendingSceneOperations() == true);
    assert(additiveCreateCount == 1);
    assert(manager.IsSceneLoaded(additiveID) == true);

    std::vector<SceneLifetime> updateOrder;
    manager.ForEachScene(
        [&updateOrder](const SceneInstanceView& instance)
        {
            updateOrder.push_back(instance.Lifetime);
        });
    assert(updateOrder.size() == 3u);
    assert(updateOrder[0] == SceneLifetime::Persistent);
    assert(updateOrder[1] == SceneLifetime::Primary);
    assert(updateOrder[2] == SceneLifetime::Additive);

    manager.RequestSceneChange(CreateScope<CountingScene>(
        40, replacementCreateCount, replacementDestroyCount));
    assert(primaryDestroyCount == 0);
    assert(manager.GetActiveSceneID() == primaryID);
    assert(manager.FlushPendingSceneOperations() == true);
    assert(primaryDestroyCount == 1);
    assert(replacementCreateCount == 1);
    assert(manager.GetPersistentSceneID() == persistentID);
    assert(manager.IsSceneLoaded(additiveID) == true);

    assert(manager.UnloadScene(additiveID) == true);
    assert(additiveDestroyCount == 0);
    assert(manager.FlushPendingSceneOperations() == true);
    assert(additiveDestroyCount == 1);
    assert(manager.IsSceneLoaded(additiveID) == false);

    // OnCreate前のLoad取消ではOnDestroyも呼ばず、未初期化Sceneを通常終了扱いしません。
    const SceneInstanceID cancelledID = manager.LoadSceneAdditive(
        CreateScope<CountingScene>(50, additiveCreateCount, additiveDestroyCount));
    assert(manager.UnloadScene(cancelledID) == true);
    assert(manager.FlushPendingSceneOperations() == false);
    assert(additiveCreateCount == 1);
    assert(additiveDestroyCount == 1);

    manager.Shutdown();
    assert(replacementDestroyCount == 1);
    assert(persistentDestroyCount == 1);

    // ========================================================================
    // 6. Application State / AudioはScene Lifetimeから独立する
    // ========================================================================
    ApplicationState applicationState;
    applicationState.Set("CurrentStage", std::string("StageA"));
    applicationState.Set("Score", std::int64_t(42));
    assert(applicationState.Contains("CurrentStage") == true);
    const ApplicationStateValue* score = applicationState.Find("Score");
    assert(score != nullptr);
    assert(std::get<std::int64_t>(*score) == 42);

    int audioStartCount = 0;
    int audioUpdateCount = 0;
    int audioStopCount = 0;
    AudioService audioService;
    assert(audioService.Install(CreateScope<CountingAudioBackend>(
        audioStartCount, audioUpdateCount, audioStopCount)) == true);
    audioService.Update(1.0f / 60.0f);
    audioService.Shutdown();
    assert(audioStartCount == 1);
    assert(audioUpdateCount == 1);
    assert(audioStopCount == 1);

    // ========================================================================
    // 7. Stage集合の差分をAdditive Load/Unloadへ変換する
    // ========================================================================
    int streamedCreateCount = 0;
    int streamedDestroyCount = 0;
    SceneFactory streamingFactory;
    assert(streamingFactory.Register("StageA", [&streamedCreateCount, &streamedDestroyCount]()
        {
            return CreateScope<CountingScene>(
                60, streamedCreateCount, streamedDestroyCount);
        }) == true);
    assert(streamingFactory.Register("StageB", [&streamedCreateCount, &streamedDestroyCount]()
        {
            return CreateScope<CountingScene>(
                70, streamedCreateCount, streamedDestroyCount);
        }) == true);

    SceneManager streamingManager;
    SceneStreamingController streaming(streamingManager, streamingFactory);
    assert(streaming.SynchronizeStages({ "StageA", "StageB" }) == true);
    assert(streamingManager.FlushPendingSceneOperations() == true);
    assert(streamedCreateCount == 2);
    assert(streamingManager.GetAdditiveSceneCount() == 2u);

    assert(streaming.SynchronizeStages({ "StageB" }) == true);
    assert(streamingManager.FlushPendingSceneOperations() == true);
    assert(streamedDestroyCount == 1);
    assert(streamingManager.GetAdditiveSceneCount() == 1u);
    streamingManager.Shutdown();
    assert(streamedDestroyCount == 2);
}

} // namespace Raven::tests
