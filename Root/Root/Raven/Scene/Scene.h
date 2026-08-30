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

    template<class... Components>
    ComponentView<Components...> View();

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
    ComponentStorageMap m_ComponentStorages;
};

// ============================================================
// ComponentStorageの取得
// ============================================================
//  Storageがなければ生成する
template<class T>
ComponentStorage<T>& Scene::GetOrCreateStorage()
{
    const std::type_index typeKey = std::type_index(typeid(T));

    const auto iterator = m_ComponentStorages.find(typeKey);

    if (iterator != m_ComponentStorages.end())
    {
        return static_cast<ComponentStorage<T>&>(*iterator->second);
    }

    auto storage = std::make_unique<ComponentStorage<T>>();
    ComponentStorage<T>* storagePointer = storage.get();

    m_ComponentStorages.emplace(typeKey, std::move(storage));

    return *storagePointer;
}

template<class T>
T* Scene::TryGetComponent(EntityIndex index)
{
    ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr) {
        return nullptr;
    }

    return storage->TryGet(index);
}

template<class T>
const T* Scene::TryGetComponent(EntityIndex index) const
{
    const ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr) {
        return nullptr;
    }

    return storage->TryGet(index);
}

// ============================================================
// SceneのComponent操作
// ============================================================
// Storageがなければnullptrを返す
template<class T>
ComponentStorage<T>* Scene::FindStorage()
{
    const std::type_index typeKey = std::type_index(typeid(T));

    const auto iterator = m_ComponentStorages.find(typeKey);

    if (iterator == m_ComponentStorages.end()) {
        return nullptr;
    }

    return static_cast<ComponentStorage<T>*>(iterator->second.get());
}

template<class T>
const ComponentStorage<T>* Scene::FindStorage() const
{
    const std::type_index typeKey = std::type_index(typeid(T));

    const auto iterator = m_ComponentStorages.find(typeKey);

    if (iterator == m_ComponentStorages.end()) {
        return nullptr;
    }

    return static_cast<const ComponentStorage<T>*>(iterator->second.get());
}

template <class T, class... Args>
T& Scene::AddComponent(EntityIndex index, Args&&... args)
{
    return GetOrCreateStorage<T>().Emplace(index, std::forward<Args>(args)...);
}

template <class T>
T& Scene::GetComponent(EntityIndex index)
{
    ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr)
    {
        throw std::runtime_error("The requested component storage does not exist.");
    }

    return storage->Get(index);
}

template <class T>
const T& Scene::GetComponent(EntityIndex index) const
{
    const ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr)
    {
        throw std::runtime_error("The requested component storage does not exist.");
    }
    return storage->Get(index);
}

template <class T>
bool Scene::HasComponent(EntityIndex index) const
{
    const ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr) {
        return false;
    }

    return storage->Has(index);
}

template <class T>
bool Scene::RemoveComponent(EntityIndex index)
{
    ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr) {
        return;
    }

    return storage->Remove(index);
}

// ============================================================
// EntityからSceneへ委譲
// ============================================================

template <class T, class... Args>
T& Entity::AddComponent(Args&&... args)
{
    if (!*this)
    {
        throw std::runtime_error("Cannot add a component to an invalid entity.");
    }

    return m_Scene->AddComponent<T>(m_Handle.m_Index, std::forward<Args>(args)...);
}

template <class T>
T& Entity::GetComponent()
{
    if (!*this)
    {
        throw std::runtime_error("Cannot get a component from an invalid entity.");
    }

    return m_Scene->GetComponent<T>(m_Handle.m_Index);
}

template <class T>
const T& Entity::GetComponent() const
{
    if (!*this)
    {
        throw std::runtime_error("Cannot get a component from an invalid entity.");
    }

    return m_Scene->GetComponent<T>(m_Handle.m_Index);
}

template <class T>
bool Entity::HasComponent() const
{
    if (!*this) {
        return false;
    }

    return m_Scene->HasComponent<T>(m_Handle.m_Index);
}

template <class T>
bool Entity::RemoveComponent()
{
    if (!*this) {
        return;
    }

    m_Scene->RemoveComponent<T>(m_Handle.m_Index);
}

// ============================================================
//  Scene::View
// ============================================================

//+----------------------------------------------------------------------
// scene.View<TransformComponent, MeshRendererComponent, TagComponent>();
// を呼ぶと、次の配列が作られます。

// std::array<const IComponentStorage*, 3>
// {
//     FindStorage<TransformComponent>(),
//     FindStorage<MeshRendererComponent>(),
//     FindStorage<TagComponent>()
// };
//
// 例えば、まだ誰もLightComponentを追加していない状態で、
// scene.View<TransformComponent, LightComponent>();
// を呼ぶとします。
//
// この場合、FindStorage<LightComponent>()はnullptrです。
//
// 条件を満たすEntityは必ず0件なので、空Viewを返します。
//
// return ComponentView<TransformComponent, LightComponent>(*this, {});
//
// この処理では、空のLightComponent Storageを新規作成しません。
//
// 前回の実装よりも余計なStorage生成を防げます。
//

template<class... Components>
ComponentView<Components...> Scene::View()
{

    static_assert(sizeof...(Components) > 0, "Scene::View requires at least one component type.");

    std::array<const IComponentStorage*, sizeof...(Components)> storages{FindStorage<Components>()...};

    for (const IComponentStorage* storage : storages)
    {
        if (storage == nullptr)
        {
            return ComponentView<Components...>(*this, nullptr);
        }
    }

    const IComponentStorage* smallestStorage = storages.front();

    for (const IComponentStorage* storage : storages)
    {
        if (storage->Size() < smallestStorage->Size())
        {
            smallestStorage = storage;
        }
    }

    return ComponentView<Components...>(*this, smallestStorage);

}

// ============================================================
// ComponentView
// ============================================================
template<class... Components>
ComponentView<Components...>::ComponentView(Scene& scene, const IComponentStorage* baseStorage)
{
    m_Scene = &scene;
    m_BaseStorage = baseStorage;
}

template<class... Components>
ComponentView<Components...>::Iterator::Iterator(Scene* scene, const IComponentStorage* baseStorage, std::size_t index)
{
    m_Scene = scene;
    m_BaseStorage = baseStorage;
    m_Index = index;
    SkipInvalidEntities();
}

template<class... Components>
void ComponentView<Components...>::Iterator::SkipInvalidEntities()
{
    if (m_BaseStorage == nullptr) {
        return;
    }

    while (m_Index < m_BaseStorage->Size())
    {
        const EntityIndex entityIndex = m_BaseStorage->GetEntityIndex(m_Index);

        const EntityGeneration generation = m_Scene->GetEntityGeneration(entityIndex);

        const bool isAlive = m_Scene->IsEntityAlive(entityIndex, generation);

        /*例えば、

            View<TransformComponent, MeshRendererComponent, TagComponent>()

            なら、次の判定に展開されます。

            HasComponent<TransformComponent>(entityID)
            &&
            HasComponent<MeshRendererComponent>(entityID)
            &&
            HasComponent<TagComponent>(entityID)
        */

        if (isAlive
            &&
            (
                m_Scene->template HasComponent<Components>(entityIndex)
                && ...
                )
            )
        {
            break;
        }

        ++m_Index;
    }

}

template<class... Components>
typename ComponentView<Components...>::Iterator&
ComponentView<Components...>::Iterator::operator++()
{
    ++m_Index;
    SkipInvalidEntities();

    return *this;
}

template<class... Components>
bool ComponentView<Components...>::Iterator::operator==(const Iterator& other) const
{
    return ((m_BaseStorage == other.m_BaseStorage) && (m_Index == other.m_Index));
}

template<class... Components>
bool ComponentView<Components...>::Iterator::operator!=(const Iterator& other) const
{
    return !(*this == other);
}

// Componentへの参照を含むタプルを返します。
// std::tuple<Entity, TransformComponent&, MeshRendererComponent&>
template<class... Components>
auto ComponentView<Components...>::Iterator::operator*() const
{
    const EntityIndex entityIndex = m_BaseStorage->GetEntityIndex(m_Index);

    const EntityHandle handle{entityIndex, m_Scene->GetEntityGeneration(entityIndex)};

    return std::tuple<Entity, Components&...>
        (
            Entity(handle, m_Scene),
            m_Scene->template GetComponent<Components>(entityIndex)...
        );
}

template<class... Components>
typename ComponentView<Components...>::Iterator
ComponentView<Components...>::begin()
{
    return Iterator(m_Scene, m_BaseStorage, 0);
}

template<class... Components>
typename ComponentView<Components...>::Iterator
ComponentView<Components...>::begin() const
{
    return Iterator(m_Scene, m_BaseStorage, 0);
}

template<class... Components>
typename ComponentView<Components...>::Iterator
ComponentView<Components...>::end() const
{

    const std::size_t endIndex = (m_BaseStorage != nullptr) ? m_BaseStorage->Size() : 0;

    return Iterator(m_Scene, m_BaseStorage, endIndex);
}

template<class... Components>
typename ComponentView<Components...>::Iterator
ComponentView<Components...>::end()
{
    const std::size_t endIndex = (m_BaseStorage != nullptr) ? m_BaseStorage->Size() : 0;

    return Iterator(m_Scene, m_BaseStorage, endIndex);
}

template<class... Components>
bool ComponentView<Components...>::Empty() const
{
    auto* nonConstThis = const_cast<ComponentView*>(this);

    return (nonConstThis->begin() == nonConstThis->end());
}

template<class... Components>
std::size_t
ComponentView<Components...>::CandidateCount() const
{
    if (m_BaseStorage == nullptr) {
        return 0;
    }

    return m_BaseStorage->Size();
}

}
