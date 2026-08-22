#include "Raven/Scene/Tests/SceneLifecycleSelfTests.h"

#include <cassert>
#include <type_traits>

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Scene/Scene.h"

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
    CountingLayer(int& attachCount, int& updateCount)
        : m_AttachCount(attachCount)
        , m_UpdateCount(updateCount)
    {
    }

    void OnAttach() override
    {
        ++m_AttachCount;
    }

    void OnUpdate(float deltaTime) override
    {
        static_cast<void>(deltaTime);
        ++m_UpdateCount;
    }

private:
    int& m_AttachCount;
    int& m_UpdateCount;
};

} // namespace

void RunSceneLifecycleSelfTests()
{
    Scene scene{};

    // ========================================================================
    // 1. Scene内部Layerは1 frameにつき1回だけUpdateされる
    // ========================================================================
    // Scene::PushLayer()がOnAttach()を担当し、毎フレームの更新入口は
    // Scene::OnUpdate() -> OnUpdateLayer()へ一本化します。
    // 派生SceneのOnUpdateGame()から同じLayerを直接更新すると二重Updateになるため、
    // 基底Sceneの契約をこのSelfTestで明示します。
    int attachCount = 0;
    int updateCount = 0;

    scene.PushLayer(CreateScope<CountingLayer>(attachCount, updateCount));
    assert(attachCount == 1);
    assert(updateCount == 0);

    scene.OnUpdate(0.0f);
    assert(updateCount == 1);

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
    // 3. Scene teardownでQueue待ちと残存Entityの両方を回収する
    // ========================================================================
    // OnDestroy()は終了時点のDestroyQueueを破棄し、EntitySlotを正規データとして現在Aliveな
    // Generationだけを直接sweepします。したがってqueuedDestroyもremainingも同じ最終安全網で
    // 無効化され、古いQueue Handleを終了後に再処理することはありません。
    scene.OnDestroy();

    assert(scene.IsEntityAlive(alreadyDestroyed) == false);
    assert(scene.IsEntityAlive(queuedDestroy) == false);
    assert(scene.IsEntityAlive(remaining) == false);

    // ========================================================================
    // 4. OnDestroy()の多重呼び出し
    // ========================================================================
    // ApplicationのScene差し替え・終了順序が将来変更されても、teardown自体は冪等であるべきです。
    // 既にAlive=falseのSlotを再破棄せず、空のDestroyQueue / ComponentStorageでも安全に終了することを
    // ここで確認します。
    scene.OnDestroy();

    assert(scene.IsEntityAlive(alreadyDestroyed) == false);
    assert(scene.IsEntityAlive(queuedDestroy) == false);
    assert(scene.IsEntityAlive(remaining) == false);
}

} // namespace Raven::tests
