#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Scene/Scene.h"

#include <cstdint>
#include <vector>

namespace Raven
{

using SceneInstanceID = std::uint64_t;
constexpr SceneInstanceID InvalidSceneInstanceID = 0u;

enum class SceneLifetime
{
    Persistent,
    Primary,
    Additive
};

struct SceneInstanceView
{
    SceneInstanceID ID = InvalidSceneInstanceID;
    SceneLifetime Lifetime = SceneLifetime::Primary;
    Scene* ScenePointer = nullptr;
};

// Runtime Sceneの所有権と安全境界での変更を一元管理します。
//
// Persistent SceneはPrimary Sceneの交換では破棄されず、Application全体で共有する
// EntityやService接続先を保持します。Additive SceneはPrimary Sceneへ重ねて読み込み、
// Stage Streamingの単位としてSceneInstanceIDで個別にUnloadできます。
//
// Frame中のCallbackから所有構造を変更しないよう、Primary交換・Additive Load/Unload・
// Persistent交換はすべてPending Operationへ積み、FlushPendingSceneOperations()だけが
// OnCreate()/OnDestroy()を実行します。
class SceneManager
{
private:
    struct OwnedSceneInstance
    {
        SceneInstanceID ID = InvalidSceneInstanceID;
        Scope<Scene> ScenePointer;
    };

public:
    SceneManager() = default;
    ~SceneManager();

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    SceneManager(SceneManager&&) = delete;
    SceneManager& operator=(SceneManager&&) = delete;

    // 起動時などFrame外の安全境界でPrimary Sceneを即時設定する互換APIです。
    void SetScene(Scope<Scene> scene);
    void RequestSceneChange(Scope<Scene> scene);

    // Application LifetimeのSceneです。nullptrで明示的に解除できます。
    void SetPersistentScene(Scope<Scene> scene);
    void RequestPersistentSceneChange(Scope<Scene> scene);

    // Additive Loadは予約時にIDを確保します。返されたIDは同Frame中のUnload予約にも使えます。
    SceneInstanceID LoadSceneAdditive(Scope<Scene> scene);
    bool UnloadScene(SceneInstanceID sceneID);

    // すべての予約操作を要求順に適用します。Scene所有構造が変化した場合だけtrueを返します。
    bool FlushPendingSceneOperations();
    // 既存呼び出しとの互換入口です。Additive/Persistent操作も同じ安全境界でflushします。
    bool FlushPendingSceneChange() { return FlushPendingSceneOperations(); }

    Scene* GetActiveScene() { return m_PrimaryScene.ScenePointer.get(); }
    const Scene* GetActiveScene() const { return m_PrimaryScene.ScenePointer.get(); }
    SceneInstanceID GetActiveSceneID() const { return m_PrimaryScene.ID; }

    Scene* GetPersistentScene() { return m_PersistentScene.ScenePointer.get(); }
    const Scene* GetPersistentScene() const { return m_PersistentScene.ScenePointer.get(); }
    SceneInstanceID GetPersistentSceneID() const { return m_PersistentScene.ID; }

    Scene* GetScene(SceneInstanceID sceneID);
    const Scene* GetScene(SceneInstanceID sceneID) const;
    bool IsSceneLoaded(SceneInstanceID sceneID) const;
    std::size_t GetAdditiveSceneCount() const { return m_AdditiveScenes.size(); }

    // Simulation/描画は背面から Persistent -> Primary -> Additive の順です。
    // Callback中にScene変更を要求しても実際のContainerはFrame末尾まで変化しません。
    template<typename Callback>
    void ForEachScene(Callback&& callback)
    {
        if (m_PersistentScene.ScenePointer != nullptr)
        {
            callback(SceneInstanceView{
                m_PersistentScene.ID, SceneLifetime::Persistent,
                m_PersistentScene.ScenePointer.get() });
        }
        if (m_PrimaryScene.ScenePointer != nullptr)
        {
            callback(SceneInstanceView{
                m_PrimaryScene.ID, SceneLifetime::Primary,
                m_PrimaryScene.ScenePointer.get() });
        }
        for (OwnedSceneInstance& instance : m_AdditiveScenes)
        {
            callback(SceneInstanceView{
                instance.ID, SceneLifetime::Additive, instance.ScenePointer.get() });
        }
    }

    template<typename Callback>
    void ForEachSceneReverse(Callback&& callback)
    {
        for (auto it = m_AdditiveScenes.rbegin(); it != m_AdditiveScenes.rend(); ++it)
        {
            callback(SceneInstanceView{
                it->ID, SceneLifetime::Additive, it->ScenePointer.get() });
        }
        if (m_PrimaryScene.ScenePointer != nullptr)
        {
            callback(SceneInstanceView{
                m_PrimaryScene.ID, SceneLifetime::Primary,
                m_PrimaryScene.ScenePointer.get() });
        }
        if (m_PersistentScene.ScenePointer != nullptr)
        {
            callback(SceneInstanceView{
                m_PersistentScene.ID, SceneLifetime::Persistent,
                m_PersistentScene.ScenePointer.get() });
        }
    }

    bool HasPendingSceneChange() const;
    bool HasPendingSceneOperations() const { return m_PendingOperations.empty() == false; }

    // 終了順は前面Additiveの逆順 -> Primary -> Persistentです。
    void Shutdown();

private:
    enum class PendingOperationType
    {
        ReplacePrimary,
        ReplacePersistent,
        LoadAdditive,
        UnloadAdditive
    };

    struct PendingOperation
    {
        PendingOperationType Type = PendingOperationType::ReplacePrimary;
        SceneInstanceID ID = InvalidSceneInstanceID;
        Scope<Scene> ScenePointer;
    };

    SceneInstanceID AllocateSceneID();
    void ActivateScene(OwnedSceneInstance& destination,
        SceneInstanceID sceneID, Scope<Scene> scene);
    static void DestroyScene(OwnedSceneInstance& instance);
    bool IsAdditiveLoaded(SceneInstanceID sceneID) const;

private:
    OwnedSceneInstance m_PersistentScene;
    OwnedSceneInstance m_PrimaryScene;
    std::vector<OwnedSceneInstance> m_AdditiveScenes;
    std::vector<PendingOperation> m_PendingOperations;
    SceneInstanceID m_NextSceneID = 1u;
};

} // namespace Raven
