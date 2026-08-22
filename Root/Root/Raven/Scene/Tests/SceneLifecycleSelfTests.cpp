#include "Raven/Scene/Tests/SceneLifecycleSelfTests.h"

#include <cassert>

#include "Raven/Scene/Scene.h"

namespace Raven::tests
{

void RunSceneLifecycleSelfTests()
{
    Scene scene{};

    // ========================================================================
    // 1. Immediate / queued / leaked-style Entityを同時に用意する
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
    // 2. Scene teardownでQueueと残存Entityの両方を回収する
    // ========================================================================
    // OnDestroy()は最初にDestroyQueueをFlushし、その後EntitySlotを正規データとして全生存Entityを
    // sweepします。したがってremainingのように所有者側で取りこぼしたEntityもここで無効になります。
    scene.OnDestroy();

    assert(scene.IsEntityAlive(alreadyDestroyed) == false);
    assert(scene.IsEntityAlive(queuedDestroy) == false);
    assert(scene.IsEntityAlive(remaining) == false);

    // ========================================================================
    // 3. OnDestroy()の多重呼び出し
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
