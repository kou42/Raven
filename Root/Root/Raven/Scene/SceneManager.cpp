#include "Raven/Scene/SceneManager.h"

#include <algorithm>

namespace Raven
{

SceneManager::~SceneManager()
{
    Shutdown();
}

void SceneManager::SetScene(Scope<Scene> scene)
{
    // 即時設定は起動用です。古い予約が後から新Sceneを上書きしないよう全予約を破棄します。
    m_PendingOperations.clear();
    DestroyScene(m_PrimaryScene);
    ActivateScene(m_PrimaryScene, AllocateSceneID(), std::move(scene));
}

void SceneManager::RequestSceneChange(Scope<Scene> scene)
{
    // Primary交換だけは従来どおり同一Frameの最後の要求を採用します。
    // Additive/Persistentの要求順は保持し、互いの操作を暗黙に取り消しません。
    for (auto it = m_PendingOperations.begin(); it != m_PendingOperations.end();)
    {
        if (it->Type == PendingOperationType::ReplacePrimary)
        {
            it = m_PendingOperations.erase(it);
        }
        else
        {
            ++it;
        }
    }

    PendingOperation operation;
    operation.Type = PendingOperationType::ReplacePrimary;
    operation.ScenePointer = std::move(scene);
    m_PendingOperations.push_back(std::move(operation));
}

void SceneManager::SetPersistentScene(Scope<Scene> scene)
{
    // 起動時の即時設定でも他種別の予約は維持し、Persistent交換予約だけを無効化します。
    for (auto it = m_PendingOperations.begin(); it != m_PendingOperations.end();)
    {
        if (it->Type == PendingOperationType::ReplacePersistent)
        {
            it = m_PendingOperations.erase(it);
        }
        else
        {
            ++it;
        }
    }
    DestroyScene(m_PersistentScene);
    ActivateScene(m_PersistentScene, AllocateSceneID(), std::move(scene));
}

void SceneManager::RequestPersistentSceneChange(Scope<Scene> scene)
{
    for (auto it = m_PendingOperations.begin(); it != m_PendingOperations.end();)
    {
        if (it->Type == PendingOperationType::ReplacePersistent)
        {
            it = m_PendingOperations.erase(it);
        }
        else
        {
            ++it;
        }
    }

    PendingOperation operation;
    operation.Type = PendingOperationType::ReplacePersistent;
    operation.ScenePointer = std::move(scene);
    m_PendingOperations.push_back(std::move(operation));
}

SceneInstanceID SceneManager::LoadSceneAdditive(Scope<Scene> scene)
{
    if (scene == nullptr)
    {
        return InvalidSceneInstanceID;
    }

    const SceneInstanceID sceneID = AllocateSceneID();
    PendingOperation operation;
    operation.Type = PendingOperationType::LoadAdditive;
    operation.ID = sceneID;
    operation.ScenePointer = std::move(scene);
    m_PendingOperations.push_back(std::move(operation));
    return sceneID;
}

bool SceneManager::UnloadScene(SceneInstanceID sceneID)
{
    if (sceneID == InvalidSceneInstanceID)
    {
        return false;
    }

    // まだOnCreate前のAdditive Loadを同じFrameで取り消す場合は、Sceneを一度も有効化しません。
    for (auto it = m_PendingOperations.begin(); it != m_PendingOperations.end(); ++it)
    {
        if (it->Type == PendingOperationType::LoadAdditive && it->ID == sceneID)
        {
            m_PendingOperations.erase(it);
            return true;
        }
    }

    if (IsAdditiveLoaded(sceneID) == false)
    {
        return false;
    }

    for (const PendingOperation& operation : m_PendingOperations)
    {
        if (operation.Type == PendingOperationType::UnloadAdditive && operation.ID == sceneID)
        {
            return false;
        }
    }

    PendingOperation operation;
    operation.Type = PendingOperationType::UnloadAdditive;
    operation.ID = sceneID;
    m_PendingOperations.push_back(std::move(operation));
    return true;
}

bool SceneManager::FlushPendingSceneOperations()
{
    if (m_PendingOperations.empty() == true)
    {
        return false;
    }

    std::vector<PendingOperation> operations = std::move(m_PendingOperations);
    m_PendingOperations.clear();

    bool changed = false;
    for (PendingOperation& operation : operations)
    {
        if (operation.Type == PendingOperationType::ReplacePrimary)
        {
            DestroyScene(m_PrimaryScene);
            ActivateScene(m_PrimaryScene, AllocateSceneID(), std::move(operation.ScenePointer));
            changed = true;
        }
        else if (operation.Type == PendingOperationType::ReplacePersistent)
        {
            DestroyScene(m_PersistentScene);
            ActivateScene(m_PersistentScene, AllocateSceneID(), std::move(operation.ScenePointer));
            changed = true;
        }
        else if (operation.Type == PendingOperationType::LoadAdditive)
        {
            OwnedSceneInstance instance;
            ActivateScene(instance, operation.ID, std::move(operation.ScenePointer));
            if (instance.ScenePointer != nullptr)
            {
                m_AdditiveScenes.push_back(std::move(instance));
                changed = true;
            }
        }
        else if (operation.Type == PendingOperationType::UnloadAdditive)
        {
            const auto it = std::find_if(m_AdditiveScenes.begin(), m_AdditiveScenes.end(),
                [&operation](const OwnedSceneInstance& instance)
                {
                    return instance.ID == operation.ID;
                });
            if (it != m_AdditiveScenes.end())
            {
                DestroyScene(*it);
                m_AdditiveScenes.erase(it);
                changed = true;
            }
        }
    }
    return changed;
}

Scene* SceneManager::GetScene(SceneInstanceID sceneID)
{
    return const_cast<Scene*>(static_cast<const SceneManager&>(*this).GetScene(sceneID));
}

const Scene* SceneManager::GetScene(SceneInstanceID sceneID) const
{
    if (sceneID == InvalidSceneInstanceID)
    {
        return nullptr;
    }
    if (m_PersistentScene.ID == sceneID)
    {
        return m_PersistentScene.ScenePointer.get();
    }
    if (m_PrimaryScene.ID == sceneID)
    {
        return m_PrimaryScene.ScenePointer.get();
    }
    const auto it = std::find_if(m_AdditiveScenes.begin(), m_AdditiveScenes.end(),
        [sceneID](const OwnedSceneInstance& instance)
        {
            return instance.ID == sceneID;
        });
    return it != m_AdditiveScenes.end() ? it->ScenePointer.get() : nullptr;
}

bool SceneManager::IsSceneLoaded(SceneInstanceID sceneID) const
{
    return GetScene(sceneID) != nullptr;
}

bool SceneManager::HasPendingSceneChange() const
{
    for (const PendingOperation& operation : m_PendingOperations)
    {
        if (operation.Type == PendingOperationType::ReplacePrimary)
        {
            return true;
        }
    }
    return false;
}

void SceneManager::Shutdown()
{
    // Pending SceneはOnCreateされていないためOnDestroyを呼ばず、所有権だけ解放します。
    m_PendingOperations.clear();

    // 描画・入力で前面にあるAdditiveから逆順で終了し、依存先のPrimary/Persistentを後まで残します。
    for (auto it = m_AdditiveScenes.rbegin(); it != m_AdditiveScenes.rend(); ++it)
    {
        DestroyScene(*it);
    }
    m_AdditiveScenes.clear();
    DestroyScene(m_PrimaryScene);
    DestroyScene(m_PersistentScene);
}

SceneInstanceID SceneManager::AllocateSceneID()
{
    // 0はInvalidとして予約します。実用上の周回時も0を返しません。
    SceneInstanceID result = m_NextSceneID++;
    if (result == InvalidSceneInstanceID)
    {
        result = m_NextSceneID++;
    }
    return result;
}

void SceneManager::ActivateScene(OwnedSceneInstance& destination,
    SceneInstanceID sceneID, Scope<Scene> scene)
{
    destination.ID = scene != nullptr ? sceneID : InvalidSceneInstanceID;
    destination.ScenePointer = std::move(scene);
    if (destination.ScenePointer != nullptr)
    {
        destination.ScenePointer->OnCreate();
    }
}

void SceneManager::DestroyScene(OwnedSceneInstance& instance)
{
    if (instance.ScenePointer == nullptr)
    {
        instance.ID = InvalidSceneInstanceID;
        return;
    }

    // 派生Scene固有Cleanupの後に基底SceneのLayer/Entity最終Sweepを必ず実行します。
    instance.ScenePointer->OnDestroy();
    instance.ScenePointer->Scene::OnDestroy();
    instance.ScenePointer.reset();
    instance.ID = InvalidSceneInstanceID;
}

bool SceneManager::IsAdditiveLoaded(SceneInstanceID sceneID) const
{
    return std::any_of(m_AdditiveScenes.begin(), m_AdditiveScenes.end(),
        [sceneID](const OwnedSceneInstance& instance)
        {
            return instance.ID == sceneID;
        });
}

} // namespace Raven
