#include "Raven/Scene/Tests/SceneLifecycleSelfTests.h"

#include <cassert>
#include <type_traits>
#include <vector>

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
}

} // namespace Raven::tests
