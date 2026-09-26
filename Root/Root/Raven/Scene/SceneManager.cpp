#include "Raven/Scene/SceneManager.h"

namespace Raven
{

SceneManager::~SceneManager()
{
    Shutdown();
}

void SceneManager::SetScene(Scope<Scene> scene)
{
    // 即時切り替えが呼ばれた場合、以前の遅延要求を後から適用して
    // 新しいSceneを意図せず上書きしないよう予約状態を先に破棄します。
    m_PendingScene.reset();
    m_HasPendingSceneChange = false;

    DestroyActiveScene();
    m_ActiveScene = std::move(scene);

    if (m_ActiveScene != nullptr)
    {
        m_ActiveScene->OnCreate();
    }
}

void SceneManager::RequestSceneChange(Scope<Scene> scene)
{
    // Callback中に現在Sceneを破棄しないことがこのAPIの重要な責務です。
    // 所有権だけをPendingへ移し、Applicationの安全なFrame境界まで待ちます。
    m_PendingScene = std::move(scene);
    m_HasPendingSceneChange = true;
}

bool SceneManager::FlushPendingSceneChange()
{
    if (m_HasPendingSceneChange == false)
    {
        return false;
    }

    // nullptrも有効な要求として扱います。
    // これにより「現在Sceneを閉じてActive Sceneなしへ移行」も同じ経路で処理できます。
    Scope<Scene> nextScene = std::move(m_PendingScene);
    m_HasPendingSceneChange = false;

    DestroyActiveScene();
    m_ActiveScene = std::move(nextScene);

    if (m_ActiveScene != nullptr)
    {
        m_ActiveScene->OnCreate();
    }

    return true;
}

void SceneManager::Shutdown()
{
    // Pending SceneはまだOnCreate()されていないためOnDestroy()を呼びません。
    m_PendingScene.reset();
    m_HasPendingSceneChange = false;
    DestroyActiveScene();
}

void SceneManager::DestroyActiveScene()
{
    if (m_ActiveScene == nullptr)
    {
        return;
    }

    // 派生Scene固有Cleanupを先に行い、その後で基底Sceneが所有する
    // Layer / Entityを最終Sweepします。Applicationの既存SetScene/終了処理と同じ順序です。
    m_ActiveScene->OnDestroy();
    m_ActiveScene->Scene::OnDestroy();
    m_ActiveScene.reset();
}

} // namespace Raven
