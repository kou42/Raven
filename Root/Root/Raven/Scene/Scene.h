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
    ~Scene() = default;

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
#if 0
template<class T>
T* Scene::TryGetComponent(EntityID index)
{
    ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr) {
        return nullptr;
    }

    return storage->TryGet(id);
}

template<class T>
const T* Scene::TryGetComponent(EntityID id) const
{
    const ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr) {
        return nullptr;
    }

    return storage->TryGet(id);
}
#endif

#endif // USE_STORAGE_VERSION_2

#if USE_STORAGE_VERSION_2
// VERSION_2では、コンポーネントの種類ごとにStorageを管理するため、GetStorageは不要です。
#elif USE_STORAGE_VERSION_1

template<class T>
ComponentStorage<T>& Scene::GetStorage()
{
    if constexpr (std::is_same_v<T, TagComponent>)
    {
        return m_Tags;
    }
    else if constexpr (std::is_same_v<T, TransformComponent>)
    {
        return m_Transforms;
    }
    else if constexpr (std::is_same_v<T, MeshRendererComponent>)
    {
        return m_MeshRenderers;
    }
    else
    {
        static_assert(AlwaysFalse<T>, "This component type is not registered in Scene.");
    }
}

template<class T>
const ComponentStorage<T>& Scene::GetStorage() const
{
    if constexpr (std::is_same_v<T, TagComponent>)
    {
        return m_Tags;
    }
    else if constexpr (std::is_same_v<T, TransformComponent>)
    {
        return m_Transforms;
    }
    else if constexpr (std::is_same_v<T, MeshRendererComponent>)
    {
        return m_MeshRenderers;
    }
    else
    {
        static_assert(AlwaysFalse<T>, "This component type is not registered in Scene.");
    }
}
#endif // USE_STORAGE_VERSION_1

// ============================================================
// SceneのComponent操作
// ============================================================

#if USE_STORAGE_VERSION_2

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

#endif

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

#if 0
template <class T, class... Args>
T& Scene::AddComponent(EntityID id, Args&&... args)
{
#if USE_STORAGE_VERSION_2
    return GetOrCreateStorage<T>().Emplace(id, std::forward<Args>(args)...);
#elif USE_STORAGE_VERSION_1
    return GetStorage<T>().Emplace(id, std::forward<Args>(args)...);
#else
    if constexpr (std::is_same_v<T, TagComponent>)
    {
        return m_Tags.emplace(id, T{ std::forward<Args>(args)... }).first->second;
    }
    else if constexpr (std::is_same_v<T, TransformComponent>)
    {
        return m_Transforms.emplace(id, T{ std::forward<Args>(args)... }).first->second;
    }
    else if constexpr (std::is_same_v<T, MeshRendererComponent>)
    {
        return m_MeshRenderers.emplace(id, T{ std::forward<Args>(args)... }).first->second;
    }
#endif
}

template <class T>
T& Scene::GetComponent(EntityID id)
{
#if USE_STORAGE_VERSION_2
    ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr)
    {
        throw std::runtime_error("The requested component storage does not exist.");
    }

    return storage->Get(id);
#elif USE_STORAGE_VERSION_1
    return GetStorage<T>().Get(id);
#else
    if constexpr (std::is_same_v<T, TagComponent>)
        return m_Tags.at(id);
    else if constexpr (std::is_same_v<T, TransformComponent>)
        return m_Transforms.at(id);
    else if constexpr (std::is_same_v<T, MeshRendererComponent>)
        return m_MeshRenderers.at(id);
#endif
}

template <class T>
const T& Scene::GetComponent(EntityID id) const
{
#if USE_STORAGE_VERSION_2
    const ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr)
    {
        throw std::runtime_error("The requested component storage does not exist.");
    }
    return storage->Get(id);
#elif USE_STORAGE_VERSION_1
    return GetStorage<T>().Get(id);
#else
    if constexpr (std::is_same_v<T, TagComponent>)
        return m_Tags.at(id);
    else if constexpr (std::is_same_v<T, TransformComponent>)
        return m_Transforms.at(id);
    else if constexpr (std::is_same_v<T, MeshRendererComponent>)
        return m_MeshRenderers.at(id);
#endif
}

template <class T>
bool Scene::HasComponent(EntityID id) const
{
#if USE_STORAGE_VERSION_2
	const ComponentStorage<T>* storage = FindStorage<T>();

	if (storage == nullptr) {
		return false;
	}

	return storage->Has(id);
#elif USE_STORAGE_VERSION_1
    return GetStorage<T>().Has(id);
#else
    if constexpr (std::is_same_v<T, TagComponent>)
        return m_Tags.find(id) != m_Tags.end();
    else if constexpr (std::is_same_v<T, TransformComponent>)
        return m_Transforms.find(id) != m_Transforms.end();
    else if constexpr (std::is_same_v<T, MeshRendererComponent>)
        return m_MeshRenderers.find(id) != m_MeshRenderers.end();
#endif
}

//反復中のComponent追加・削除
//次のような使い方は避けてください。
#if 0
for (auto [entity, transform] : scene.View<TransformComponent>())
{
    entity.RemoveComponent<TransformComponent>();
}
#endif
//基準StorageのIteratorを無効化する可能性があります。
//安全な方法は、後で削除するEntityを保存することです。
#if 0
std::vector<Entity> removeList;

for (auto [entity, transform] : scene.View<TransformComponent>())
{
    if (transform.Position.y < -100.0f) {
        removeList.push_back(entity);
    }
}

for (Entity entity : removeList)
{
    entity.RemoveComponent<TransformComponent>();
}
#endif 
template <class T>
bool Scene::RemoveComponent(EntityID id)
{
#if USE_STORAGE_VERSION_2
    ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr) {
        return;
    }

    return storage->Remove(id);
#elif USE_STORAGE_VERSION_1
    GetStorage<T>().Remove(id);
#else
    if constexpr (std::is_same_v<T, TagComponent>)
        m_Tags.erase(id);
    else if constexpr (std::is_same_v<T, TransformComponent>)
        m_Transforms.erase(id);
    else if constexpr (std::is_same_v<T, MeshRendererComponent>)
        m_MeshRenderers.erase(id);
#endif
}
#endif

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
    //return m_Scene->AddComponent<T>(m_ID, std::forward<Args>(args)...);
}

template <class T>
T& Entity::GetComponent()
{
    if (!*this)
    {
        throw std::runtime_error("Cannot get a component from an invalid entity.");
    }

    return m_Scene->GetComponent<T>(m_Handle.m_Index);
    //return m_Scene->GetComponent<T>(m_ID);
}

template <class T>
const T& Entity::GetComponent() const
{
    if (!*this)
    {
        throw std::runtime_error("Cannot get a component from an invalid entity.");
    }

    return m_Scene->GetComponent<T>(m_Handle.m_Index);
    //return m_Scene->GetComponent<T>(m_ID);
}

template <class T>
bool Entity::HasComponent() const
{
    if (!*this) {
        return false;
    }

    return m_Scene->HasComponent<T>(m_Handle.m_Index);
    //return m_Scene->HasComponent<T>(m_ID);
}

template <class T>
bool Entity::RemoveComponent()
{
    if (!*this) {
        return;
    }

    m_Scene->RemoveComponent<T>(m_Handle.m_Index);
    //m_Scene->RemoveComponent<T>(m_ID);
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

#if 0
    static_assert(sizeof...(Components) > 0, "Scene::View requires at least one component type.");

    std::array<const IComponentStorage*, sizeof...(Components)> storages{
        FindStorage<Components>()...
    };

    // 一つでもStorageが存在しなければ、
    // 条件を満たすEntityは存在しない。
    for (const IComponentStorage* storage : storages)
    {
        if (storage == nullptr)
        {
            return ComponentView<Components...>(*this, {});
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

    std::vector<EntityID> entityIDs;

    entityIDs.reserve(smallestStorage->Size());
    smallestStorage->CollectEntityIDs(entityIDs);

    // 最小Storageに含まれるEntity IDを取得します。
    // smallestStorage->CollectEntityIDs(entityIDs);
    // その後、全Componentを持たないEntityを削除します。
    // [this](EntityID entityID)
    // {
    //     return !(
    //         HasComponent<Components>(entityID)
    //         && ...
    //     );
    // }
    //          
    // パラメータパックは、例えば次へ展開されます。
    // return !(
    //    HasComponent<TransformComponent>(entityID)
    //    &&
    //    HasComponent<MeshRendererComponent>(entityID)
    //    &&
    //    HasComponent<TagComponent>(entityID)
    // );

    auto func = [this](EntityID entityID)
    {
        return !(HasComponent<Components>(entityID) && ...);
    };

    entityIDs.erase(std::remove_if(entityIDs.begin(), entityIDs.end(), func, entityIDs.end());

    return ComponentView<Components...>(*this, std::move(entityIDs));
#endif

}

// ============================================================
// ComponentView
// ============================================================
#if 0
template<class... Components>
ComponentView<Components...>::ComponentView(Scene& scene, std::vector<EntityID> entities)
{
    m_Scene = &scene;
    m_Entities = std::move(entities);
}
#endif

template<class... Components>
ComponentView<Components...>::ComponentView(Scene& scene, const IComponentStorage* baseStorage)
{
    m_Scene = &scene;
    m_BaseStorage = baseStorage;
}

#if 0
template<class... Components>
ComponentView<Components...>::Iterator::Iterator(Scene* scene, const std::vector<EntityID>* entities, std::size_t index)
{
    m_Scene = scene
    m_Entities = entities;
    m_Index = index;
}
#endif

template<class... Components>
ComponentView<Components...>::Iterator::Iterator(Scene* scene, const IComponentStorage* baseStorage, std::size_t index)
{
    m_Scene = scene;
    m_BaseStorage = baseStorage;
    m_Index = index;
    SkipInvalidEntities();
}

#if 0
template<class... Components>
typename ComponentView<Components...>::Iterator&
ComponentView<Components...>::Iterator::operator++()
{
    ++m_Index;
    return *this;
}

template<class... Components>
bool ComponentView<Components...>::Iterator::operator==(const Iterator& other) const
{
    return (m_Entities == other.m_Entities && m_Index == other.m_Index);
}

template<class... Components>
bool ComponentView<Components...>::Iterator::operator!=(const Iterator& other) const
{
    return !(*this == other);
}

// View<TransformComponent, MeshRendererComponent>()
// なら、次のタプルを返します。
//
//std::tuple<Entity, TransformComponent&, MeshRendererComponent&>
//
//構造化束縛で受け取れます。
//for (auto [entity, transform, meshRenderer] : view)
//{
//}
//
//Componentは参照なので、変更できます。
//
//transform.Position.x += 1.0f;

template<class... Components>
auto ComponentView<Components...>::Iterator::operator*() const
{
    const EntityID entityID = (*m_Entities)[m_Index];

    return std::tuple<Entity,Components&...>(
        Entity(entityID, m_Scene),
        m_Scene->template GetComponent<Components>(entityID)...
    );
}

template<class... Components>
typename ComponentView<Components...>::Iterator
ComponentView<Components...>::begin()
{
    return Iterator(m_Scene, &m_Entities, 0);
}

template<class... Components>
typename ComponentView<Components...>::Iterator
ComponentView<Components...>::end()
{
    return Iterator(m_Scene, &m_Entities, m_Entities.size());
}

template<class... Components>
bool ComponentView<Components...>::Empty() const
{
    return m_Entities.empty();
}

template<class... Components>
std::size_t ComponentView<Components...>::Size() const
{
    return m_Entities.size();
}

#endif

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

#if 0
    while (m_Index < m_BaseStorage->Size())
    {
        const EntityID entityID = m_BaseStorage->GetEntityID(m_Index);

        /*例えば、

            View<TransformComponent, MeshRendererComponent, TagComponent>()

            なら、次の判定に展開されます。

            HasComponent<TransformComponent>(entityID)
            &&
            HasComponent<MeshRendererComponent>(entityID)
            &&
            HasComponent<TagComponent>(entityID)
        */
        if ((m_Scene->template HasComponent<Components>(entityID) && ...))
        {
            break;
        }

        ++m_Index;
    }
#endif

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

#if 0
template<class... Components>
bool ComponentView<Components...>::Empty() const
{
    return begin() == end();
}
#endif

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

#if 0
// ============================================================
//  Scene::View
// ============================================================
template<class FirstComponent, class... OtherComponents>
ComponentView<FirstComponent, OtherComponents...>
Scene::View()
{
    ComponentStorage<FirstComponent>* storage = FindStorage<FirstComponent>();

    if (storage == nullptr)
    {
        storage = &GetOrCreateStorage<FirstComponent>();
    }

    return ComponentView<FirstComponent, OtherComponents...>(*this, *storage);

}

// ============================================================
// ComponentView
// ============================================================

template<class FirstComponent, class... OtherComponents>
ComponentView<FirstComponent, OtherComponents...>::ComponentView(
    Scene& scene,
    BaseStorage& baseStorage
)
{
    m_Scene = &scene;
    m_BaseStorage = &baseStorage;
}

template<class FirstComponent, class... OtherComponents>
ComponentView<FirstComponent, OtherComponents...>::Iterator::Iterator(
    Scene* scene,
    BaseIterator current,
    BaseIterator end
)
{
    m_Scene = scene;
    m_Current = current;
    m_End = end;
    SkipInvalidEntities();
}

// 現在位置を一つ進め、そのEntityが全Componentを持たなければさらに読み飛ばします。
template<class FirstComponent, class... OtherComponents>
typename ComponentView<FirstComponent, OtherComponents...>::Iterator& ComponentView<FirstComponent, OtherComponents...>::Iterator::operator++()
{
    ++m_Current;
    SkipInvalidEntities();
    return *this;
}

template<class FirstComponent, class... OtherComponents>
bool ComponentView<FirstComponent, OtherComponents...>::Iterator::operator==(const Iterator& other) const
{
    return m_Current == other.m_Current;
}

template<class FirstComponent, class... OtherComponents>
bool ComponentView<FirstComponent, OtherComponents...>::Iterator::operator!=(const Iterator& other) const
{
    return !(*this == other);
}

// 条件に合わないEntityを読み飛ばす
template<class FirstComponent, class... OtherComponents>
void ComponentView<FirstComponent, OtherComponents...>::Iterator::SkipInvalidEntities()
{
    while (m_Current != m_End)
    {
        const EntityID entityID = m_Current->first;

#if 0
        ここで重要なのが、

        m_Scene->template HasComponent<OtherComponents>(entityID)

        のtemplateです。

        テンプレートクラス内から、テンプレートメンバ関数を呼んでいるため必要です。

        例えば、

        scene.View<TransformComponent>()

        の場合、OtherComponents...は空になります。

        fold expression

        (condition && ...)

        は、空パックの場合にtrueになります。

        したがって、

        View<TransformComponent>()

        は、Transformを持つ全Entityをそのまま列挙できます。

        特別な分岐は不要です。
#endif
        if ((m_Scene->template HasComponent<OtherComponents>(entityID) && ...))
        {
            break;
        }

        ++m_Current;
    }
}

template<class FirstComponent, class... OtherComponents>
auto ComponentView<FirstComponent, OtherComponents...>::Iterator::operator*() const
{
    const EntityID entityID = m_Current->first;

    return std::tuple<Entity, FirstComponent&, OtherComponents&...>(
        Entity(entityID, m_Scene),
        m_Current->second,
        m_Scene->template GetComponent<OtherComponents>(entityID)...
    );
}

template<class FirstComponent, class... OtherComponents>
typename ComponentView<FirstComponent, OtherComponents...>::Iterator ComponentView<FirstComponent, OtherComponents...>::begin()
{
    return Iterator(m_Scene, m_BaseStorage->begin(), m_BaseStorage->end());
}

template<class FirstComponent, class... OtherComponents>
typename ComponentView<FirstComponent, OtherComponents...>::Iterator ComponentView<FirstComponent, OtherComponents...>::end()
{
    return Iterator(m_Scene, m_BaseStorage->end(), m_BaseStorage->end());
}
#endif

}
