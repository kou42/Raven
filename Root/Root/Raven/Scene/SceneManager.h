#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{

// ============================================================================
// SceneManager
// ============================================================================
// Runtime Sceneの所有権と切り替え要求を一元管理します。
//
// SetScene()はApplication起動時など、Sceneを即時に有効化してよい境界で使用します。
// RequestSceneChange()はUpdate / Event / UI callback中から呼ぶための予約APIです。
// 予約されたSceneはFlushPendingSceneChange()が呼ばれるまでActive Sceneへ影響しません。
//
// Scene破棄は派生OnDestroy()の後に基底Scene::OnDestroy()も明示的に呼びます。
// これにより派生Sceneがbase呼び出しを忘れても、Scene内部Layerと残存Entityの
// 共通Cleanupを必ず実行できます。
class SceneManager
{
public:
    SceneManager() = default;
    ~SceneManager();

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    SceneManager(SceneManager&&) = delete;
    SceneManager& operator=(SceneManager&&) = delete;

    // 即時にActive Sceneを差し替えます。
    // Frame処理中ではなく、起動・終了・明示的な安全境界から呼び出してください。
    void SetScene(Scope<Scene> scene);

    // 次の安全なFrame境界で切り替えるSceneを予約します。
    // 同一Frame中に複数回要求された場合は最後の要求を採用します。
    void RequestSceneChange(Scope<Scene> scene);

    // 予約されたScene切り替えを実行します。
    // 実際に切り替えを行った場合だけtrueを返します。
    bool FlushPendingSceneChange();

    Scene* GetActiveScene() { return m_ActiveScene.get(); }
    const Scene* GetActiveScene() const { return m_ActiveScene.get(); }

    bool HasPendingSceneChange() const { return m_HasPendingSceneChange; }

    // Application終了時などにActive/Pending Sceneを明示的に解放します。
    void Shutdown();

private:
    void DestroyActiveScene();

private:
    Scope<Scene> m_ActiveScene;
    Scope<Scene> m_PendingScene;
    bool m_HasPendingSceneChange = false;
};

} // namespace Raven
