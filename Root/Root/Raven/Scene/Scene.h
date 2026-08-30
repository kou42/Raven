#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <typeindex>
#include <utility>
#include <algorithm>
#include <array>

//#include "Raven/Core/Event.h"
#include "Raven/Core/Base.h"
#include "Raven/Renderer/Layer/Layer.h"

#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/ComponentStorage.h"
#include "Raven/Scene/ComponentView.h"

#include "Raven/Physics/PhysicsWorld.h"

// コンポーネント汎用化
#define USE_STORAGE_VERSION_2 1
#define USE_STORAGE_VERSION_1 1

namespace Raven
{
class Scene
{

public:

    using ComponentStorageMap = std::unordered_map< std::type_index, std::unique_ptr<IComponentStorage>>;

public:
    //Scene() = default;
    Scene();

    // ApplicationはScope<Scene>としてSceneGame等の派生Sceneを所有します。
    // 基底Sceneポインタ経由で破棄した際にも派生デストラクタを確実に呼ぶためvirtualにします。
    virtual ~Scene() = default;

    //+---------------------------------------------------------------------
    // 仮想関数
    //+---------------------------------------------------------------------
    // GLリソース、Entity生成、Texture/Shader読み込み
    virtual void OnCreate();
	// GLリソース解放、Entity破棄、Texture/Shader解放
    virtual void OnDestroy();
    // 入力、物理演算、ゲームロジックなど
    virtual void OnUpdate(float dt);
    // 描画処理
    virtual void OnRender();
    virtual void OnEvent(Event& e);

    //+---------------------------------------------------------------------
    // メンバ関数
    //+---------------------------------------------------------------------
    void PushLayer(Scope<Layer> layer);
    void RenderEntities();

    Entity CreateEntity(const std::string& name = "Entity");
    void DestroyEntity(Entity entity);

    void QueueDestroyEntity(Entity entity);
    void FlushDestroyedEntities();

    // EntityIndexから現在生存しているGenerationを復元し、安全なEntity Handleを返します。
    // Picking AttachmentにはEntityIndexだけを書き込むため、Editor側はこのAPIを通して
    // GenerationとSceneポインタを含む正式なEntityへ戻します。
    // Indexが無効・範囲外・既にDestroy済みの場合はInvalid Entityを返します。
    Entity TryGetEntity(EntityIndex index);

    // ========================================================================
    // Physics World Access
    // ========================================================================
    // Sceneが所有しているPhysicsWorldへアクセスします。
    //
    // ゲーム側からRayCast / AddImpulse / QueryAABBなどの
    // PhysicsWorld公開APIを利用するための正式な入口です。
    //
    // PhysicsWorldそのものの所有権はSceneが保持します。
    const ph::PhysicsWorld& GetPhysicsWorld() const;
    ph::PhysicsWorld& GetPhysicsWorld();

    //+---------------------------------------------------------------------
    // テンプレート
    //+---------------------------------------------------------------------
    template <class T, class... Args>
    T& AddComponent(EntityIndex index, Args&&... args);

    template <class T>
    T& GetComponent(EntityIndex index);

    template <class T>
    const T& GetComponent(EntityIndex index) const;

    template <class T>
    bool HasComponent(EntityIndex index) const;

    template <class T>
    bool RemoveComponent(EntityIndex index);

    template<class T>
    T* TryGetComponent(EntityIndex index);

    template<class T>
    const T* TryGetComponent(EntityIndex index) const;

#if USE_STORAGE_VERSION_2

    //template<class T>
    //T* TryGetComponent(EntityID id);

    //template<class T>
    //const T* TryGetComponent(EntityID id) const;

    //template<class FirstComponent, class... OtherComponents>
    //ComponentView<FirstComponent, OtherComponents...> View();
    template<class... Components>
    ComponentView<Components...> View();
#endif

#if USE_STORAGE_VERSION_2
	// VERSION_2では、コンポーネントの種類ごとにStorageを管理するため、GetStorageは不要です。
#elif USE_STORAGE_VERSION_1
    // コンポーネント汎用化
    const ComponentStorage<TagComponent>& GetTags() const
    {
        return m_Tags;
    }

    const ComponentStorage<TransformComponent>& GetTransforms() const
    {
        return m_Transforms;
    }

    const ComponentStorage<MeshRendererComponent>& GetMeshRenderers() const
    {
        return m_MeshRenderers;
    }
#else
    const std::unordered_map<EntityID, TagComponent>& GetTags() const
    {
        return m_Tags;
    }

    const std::unordered_map<EntityID, TransformComponent>& GetTransforms() const
    {
        return m_Transforms;
    }

    const std::unordered_map<EntityID, MeshRendererComponent>& GetMeshRenderers() const
    {
        return m_MeshRenderers;
    }
#endif

protected:
    std::vector<Scope<Layer>> m_layers;

protected:

    virtual void OnUpdateGame(float dt) {}

private:

    struct EntitySlot
    {
        EntityGeneration Generation = 0;
        bool Alive = false;
    };

    // 各Entity IndexのGenerationと生存状態
    std::vector<EntitySlot> m_EntitySlots;

    // 破棄され、再利用可能なIndex
    std::vector<EntityIndex> m_FreeEntityIndices;

    //std::vector<EntityIndex> m_DestroyQueue;
    //Destroy QueueにIndexだけを保存すると、同じIndexが再利用された後に誤って新しいEntityを破棄する危険があります。
    //そのため、キューにはEntityハンドル全体を保存します。
    std::vector<Entity> m_DestroyQueue;

    ph::PhysicsWorld m_PhysicsWorld;

    float m_PhysicsAccumulator = 0.0f;
    float m_FixedDeltaTime = 1.0f / 60.0f;

private:

    template<class>
    inline static constexpr bool AlwaysFalse = false;

    template<class T>
    ComponentStorage<T>& GetStorage();

    template<class T>
    const ComponentStorage<T>& GetStorage() const;

    // Storageがなければ生成する
    template<class T>
    ComponentStorage<T>& GetOrCreateStorage();

    //  Storageがなければnullptrを返す
    template<class T>
    ComponentStorage<T>* FindStorage();

    template<class T>
    const ComponentStorage<T>* FindStorage() const;

private:

	void OnUpdatePhysics(float dt);
    void OnUpdateLayer(float dt);
    
public:

    bool IsEntityAlive(EntityIndex index, EntityGeneration generation) const;
    bool IsEntityAlive(EntityHandle handle) const;
    bool IsEntityAlive(Entity entity) const;

    EntityGeneration GetEntityGeneration(EntityIndex index) const;

private:
    EntityIndex m_NextEntityIndex = 1;

#if USE_STORAGE_VERSION_2
    ComponentStorageMap m_ComponentStorages;
#elif USE_STORAGE_VERSION_1
    ComponentStorage<TagComponent> m_Tags;
    ComponentStorage<TransformComponent> m_Transforms;
    ComponentStorage<MeshRendererComponent> m_MeshRenderers;
#else
    std::unordered_map<EntityID, TagComponent> m_Tags;
    std::unordered_map<EntityID, TransformComponent> m_Transforms;
    std::unordered_map<EntityID, MeshRendererComponent> m_MeshRenderers;
#endif
};

// ============================================================
// ComponentStorageの取得
// ============================================================

#if USE_STORAGE_VERSION_2

template<class T>
ComponentStorage<T>& Scene::GetStorage()
{
    const std::type_index key(typeid(T));

    auto it = m_ComponentStorages.find(key);

    if (it == m_ComponentStorages.end())
    {
        throw std::runtime_error("Component storage not found.");
    }

    return *static_cast<ComponentStorage<T>*>(it->second.get());
}

template<class T>
const ComponentStorage<T>& Scene::GetStorage() const
{
    const std::type_index key(typeid(T));

    auto it = m_ComponentStorages.find(key);

    if (it == m_ComponentStorages.end())
    {
        throw std::runtime_error("Component storage not found.");
    }

    return *static_cast<const ComponentStorage<T>*>(it->second.get());
}

template<class T>
ComponentStorage<T>& Scene::GetOrCreateStorage()
{
    const std::type_index key(typeid(T));

    auto it = m_ComponentStorages.find(key);

    if (it == m_ComponentStorages.end())
    {
        auto storage = std::make_unique<ComponentStorage<T>>();
        ComponentStorage<T>* raw = storage.get();
        m_ComponentStorages.emplace(key, std::move(storage));
        return *raw;
    }

    return *static_cast<ComponentStorage<T>*>(it->second.get());
}

template<class T>
ComponentStorage<T>* Scene::FindStorage()
{
    const std::type_index key(typeid(T));

    auto it = m_ComponentStorages.find(key);

    if (it == m_ComponentStorages.end())
    {
        return nullptr;
    }

    return static_cast<ComponentStorage<T>*>(it->second.get());
}

template<class T>
const ComponentStorage<T>* Scene::FindStorage() const
{
    const std::type_index key(typeid(T));

    auto it = m_ComponentStorages.find(key);

    if (it == m_ComponentStorages.end())
    {
        return nullptr;
    }

    return static_cast<const ComponentStorage<T>*>(it->second.get());
}

template <class T, class... Args>
T& Scene::AddComponent(EntityIndex index, Args&&... args)
{
    return GetOrCreateStorage<T>().Emplace(index, std::forward<Args>(args)...);
}

template <class T>
T& Scene::GetComponent(EntityIndex index)
{
    return GetStorage<T>().Get(index);
}

template <class T>
const T& Scene::GetComponent(EntityIndex index) const
{
    return GetStorage<T>().Get(index);
}

template <class T>
bool Scene::HasComponent(EntityIndex index) const
{
    const ComponentStorage<T>* storage = FindStorage<T>();
    if (storage == nullptr)
    {
        return false;
    }

    return storage->Has(index);
}

template <class T>
bool Scene::RemoveComponent(EntityIndex index)
{
    ComponentStorage<T>* storage = FindStorage<T>();
    if (storage == nullptr)
    {
        return false;
    }

    return storage->Remove(index);
}

template<class T>
T* Scene::TryGetComponent(EntityIndex index)
{
    ComponentStorage<T>* storage = FindStorage<T>();
    if (storage == nullptr)
    {
        return nullptr;
    }

    return storage->TryGet(index);
}

template<class T>
const T* Scene::TryGetComponent(EntityIndex index) const
{
    const ComponentStorage<T>* storage = FindStorage<T>();
    if (storage == nullptr)
    {
        return nullptr;
    }

    return storage->TryGet(index);
}

template<class... Components>
ComponentView<Components...> Scene::View()
{
    return ComponentView<Components...>(GetOrCreateStorage<Components>()...);
}

#endif

}