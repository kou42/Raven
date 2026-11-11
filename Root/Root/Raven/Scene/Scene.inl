#pragma once

namespace Raven
{

template <class T>
T* Scene::FindLayer()
{
    for (const Scope<Layer>& layer : m_layers)
    {
        T* typedLayer = dynamic_cast<T*>(layer.get());
        if (typedLayer != nullptr)
        {
            return typedLayer;
        }
    }
    return nullptr;
}

template <class T>
const T* Scene::FindLayer() const
{
    for (const Scope<Layer>& layer : m_layers)
    {
        const T* typedLayer = dynamic_cast<const T*>(layer.get());
        if (typedLayer != nullptr)
        {
            return typedLayer;
        }
    }
    return nullptr;
}

inline bool Scene::CanSnapshotEntityComponents(EntityHandle handle) const
{
    if (IsEntityAlive(handle) == false)
    {
        return false;
    }

    const std::array<std::type_index, 10> supported{
        typeid(TagComponent), typeid(TransformComponent), typeid(CameraComponent),
        typeid(MeshRendererComponent), typeid(MeshDeformationComponent),
        typeid(AnimatorComponent), typeid(RigidBodyComponent), typeid(ColliderComponent),
        typeid(RelationshipComponent), typeid(LocalTransformComponent)
    };
    for (const auto& entry : m_ComponentStorages)
    {
        if (entry.second->Has(handle.m_Index) == false)
        {
            continue;
        }
        if (std::find(supported.begin(), supported.end(), entry.first) == supported.end())
        {
            return false;
        }
    }
    return true;
}

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
    if (index == InvalidEntityIndex || static_cast<std::size_t>(index) >= m_EntitySlots.size()
        || m_EntitySlots[index].Alive == false)
    {
        return nullptr;
    }

    ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr) {
        return nullptr;
    }

    return storage->TryGet(index);
}

template<class T>
const T* Scene::TryGetComponent(EntityIndex index) const
{
    if (index == InvalidEntityIndex || static_cast<std::size_t>(index) >= m_EntitySlots.size()
        || m_EntitySlots[index].Alive == false)
    {
        return nullptr;
    }

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
    if (index == InvalidEntityIndex || static_cast<std::size_t>(index) >= m_EntitySlots.size()
        || m_EntitySlots[index].Alive == false)
    {
        throw std::out_of_range("Cannot access components of a dead entity index.");
    }

    if constexpr (std::is_same_v<T, LocalTransformComponent>)
    {
        if (CanUseLocalTransform(index) == false)
        {
            throw std::invalid_argument("Local Transform requires a Transform component.");
        }
    }
    if constexpr (std::is_same_v<T, RigidBodyComponent> || std::is_same_v<T, LocalTransformComponent>)
    {
        // 追加順序で制約を回避しないよう、候補を構築してから幾何契約を検証します。
        // 検証はキャッシュや正規入力を書き換えず、失敗時は候補Componentだけを除去します。
        auto& storage = GetOrCreateStorage<T>();
        storage.Emplace(index, std::forward<Args>(args)...);
        try
        {
            if (HasComponent<LocalTransformComponent>(index) == true
                && m_TransformSystem.CanResolve(*this, EntityHandle{index, m_EntitySlots[index].Generation}) == false)
            {
                throw std::invalid_argument("Local simulation Transform is not representable or has unsupported scale.");
            }
        }
        catch (...)
        {
            storage.Remove(index);
            throw;
        }
        return storage.Get(index);
    }
    return GetOrCreateStorage<T>().Emplace(index, std::forward<Args>(args)...);
}

template <class T>
T& Scene::GetComponent(EntityIndex index)
{
    if (index == InvalidEntityIndex || static_cast<std::size_t>(index) >= m_EntitySlots.size()
        || m_EntitySlots[index].Alive == false)
    {
        throw std::out_of_range("Cannot access components of a dead entity index.");
    }

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
    if (index == InvalidEntityIndex || static_cast<std::size_t>(index) >= m_EntitySlots.size()
        || m_EntitySlots[index].Alive == false)
    {
        throw std::out_of_range("Cannot access components of a dead entity index.");
    }

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
    if (index == InvalidEntityIndex || static_cast<std::size_t>(index) >= m_EntitySlots.size()
        || m_EntitySlots[index].Alive == false)
    {
        return false;
    }

    const ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr) {
        return false;
    }

    return storage->Has(index);
}

template <class T>
bool Scene::RemoveComponent(EntityIndex index)
{
    if (index == InvalidEntityIndex || static_cast<std::size_t>(index) >= m_EntitySlots.size()
        || m_EntitySlots[index].Alive == false)
    {
        return false;
    }

    if constexpr (std::is_same_v<T, LocalTransformComponent>)
    {
        return DisableLocalTransform(EntityHandle{index, m_EntitySlots[index].Generation});
    }
    if constexpr (std::is_same_v<T, TransformComponent>)
    {
        if (CanRemoveTransform(index) == false)
        {
            return false;
        }
    }
    if constexpr (std::is_same_v<T, RelationshipComponent>)
    {
        if (HasComponent<LocalTransformComponent>(index) == true && HasComponent<RelationshipComponent>(index) == true
            && DetachEntity(EntityHandle{index, m_EntitySlots[index].Generation}) == false)
        {
            return false;
        }
    }
    ComponentStorage<T>* storage = FindStorage<T>();

    if (storage == nullptr)
    {
        // Storage未生成時は削除対象が存在しないため、削除失敗を返します。
        return false;
    }

    return storage->Remove(index);
}

// ============================================================
// EntityからSceneへ委譲
// ============================================================

template <class T, class... Args>
T& Entity::AddComponent(Args&&... args)
{
    if (static_cast<bool>(*this) == false)
    {
        throw std::runtime_error("Cannot add a component to an invalid entity.");
    }

    return m_Scene->AddComponent<T>(m_Handle.m_Index, std::forward<Args>(args)...);
}

template <class T>
T& Entity::GetComponent()
{
    if (static_cast<bool>(*this) == false)
    {
        throw std::runtime_error("Cannot get a component from an invalid entity.");
    }

    return m_Scene->GetComponent<T>(m_Handle.m_Index);
}

template <class T>
const T& Entity::GetComponent() const
{
    if (static_cast<bool>(*this) == false)
    {
        throw std::runtime_error("Cannot get a component from an invalid entity.");
    }

    return m_Scene->GetComponent<T>(m_Handle.m_Index);
}

template <class T>
bool Entity::HasComponent() const
{
    if (static_cast<bool>(*this) == false) {
        return false;
    }

    return m_Scene->HasComponent<T>(m_Handle.m_Index);
}

template <class T>
bool Entity::RemoveComponent()
{
    if (static_cast<bool>(*this) == false)
    {
        // 無効なEntityからSceneへアクセスせず、削除失敗を返します。
        return false;
    }

    // Storageで実際に削除できたかを呼び出し元まで伝えます。
    return m_Scene->RemoveComponent<T>(m_Handle.m_Index);
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
ComponentView<Components...> Scene::View(ComponentViewStatistics* statistics)
{

    static_assert(sizeof...(Components) > 0, "Scene::View requires at least one component type.");

    typename ComponentView<Components...>::StorageTuple typedStorages{FindStorage<Components>()...};
    const auto storages = std::apply([](auto*... storage)
    {
        return std::array<const IComponentStorage*, sizeof...(Components)>{storage...};
    }, typedStorages);
    if (statistics != nullptr)
    {
        statistics->StorageLookups += sizeof...(Components);
    }

    for (const IComponentStorage* storage : storages)
    {
        if (storage == nullptr)
        {
            return ComponentView<Components...>(*this, nullptr, typedStorages, statistics);
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

    return ComponentView<Components...>(*this, smallestStorage, typedStorages, statistics);

}

// ============================================================
// ComponentView
// ============================================================
template<class... Components>
ComponentView<Components...>::ComponentView(Scene& scene, const IComponentStorage* baseStorage, ComponentViewStatistics* statistics)
    : ComponentView(scene, baseStorage, StorageTuple{scene.template FindStorage<Components>()...}, statistics)
{
    if (statistics != nullptr)
    {
        statistics->StorageLookups += sizeof...(Components);
    }
}

template<class... Components>
ComponentView<Components...>::ComponentView(Scene& scene, const IComponentStorage* baseStorage,
    StorageTuple storages, ComponentViewStatistics* statistics)
{
    m_Scene = &scene;
    m_BaseStorage = baseStorage;
    m_Statistics = statistics;
    m_Storages = storages;
    if (baseStorage != nullptr && std::apply([&](auto*... storage)
        { return ((static_cast<const IComponentStorage*>(storage) == baseStorage) || ...); }, storages) == false)
    {
        throw std::invalid_argument("View base storage does not belong to its component storages.");
    }
#ifndef NDEBUG
    m_LifetimeVersion = scene.m_ViewLifetimeVersion;
    m_Versions = std::apply([](auto*... storage)
    {
        return std::array<uint64_t, sizeof...(Components)>{(storage != nullptr ? storage->GetStructuralVersion() : 0)...};
    }, m_Storages);
#endif
}

#ifndef NDEBUG
template<class... Components>
void ComponentView<Components...>::ValidateState(const Scene* scene, const StorageTuple& storages,
    const std::array<uint64_t, sizeof...(Components)>& versions, uint64_t lifetimeVersion,
    ComponentViewStatistics* statistics)
{
    if (scene != nullptr && scene->m_ViewLifetimeVersion != lifetimeVersion)
    {
        // Storage自体の生存確認を最優先し、解放済みpointeeのVersionを読まないようにします。
        throw std::logic_error("View was invalidated by Scene shutdown; reacquire it.");
    }
    std::size_t index = 0;
    std::apply([&](auto*... storage)
    {
        ([&]()
        {
            if (storage != nullptr)
            {
                if (statistics != nullptr)
                {
                    ++statistics->StructuralChecks;
                }
                if (storage->GetStructuralVersion() != versions[index])
                {
                    throw std::logic_error("View was invalidated by a component structural change; reacquire it.");
                }
            }
            ++index;
        }(), ...);
    }, storages);
}
#endif

template<class... Components>
void ComponentView<Components...>::Validate() const
{
#ifndef NDEBUG
    ValidateState(m_Scene, m_Storages, m_Versions, m_LifetimeVersion, m_Statistics);
#endif
}

template<class... Components>
void ComponentView<Components...>::Iterator::Validate() const
{
#ifndef NDEBUG
    ComponentView::ValidateState(m_Scene, m_Storages, m_Versions, m_LifetimeVersion, m_Statistics);
#endif
}

template<class... Components>
ComponentView<Components...>::Iterator::Iterator(Scene* scene, const IComponentStorage* baseStorage, std::size_t index,
    ComponentViewStatistics* statistics)
{
    m_Scene = scene;
    m_BaseStorage = baseStorage;
    m_Index = index;
    m_Statistics = statistics;
    if (scene != nullptr)
    {
        // 互換ConstructorもComponent配列ではなくStorageのpointeeを解決して保存します。
        *this = Iterator(ComponentView(*scene, baseStorage, statistics), index);
    }
    else if (baseStorage != nullptr)
    {
        throw std::invalid_argument("A nonempty View iterator requires a Scene.");
    }
}

template<class... Components>
ComponentView<Components...>::Iterator::Iterator(const ComponentView& view, std::size_t index)
    : m_Scene(view.m_Scene), m_BaseStorage(view.m_BaseStorage), m_Index(index),
      m_Statistics(view.m_Statistics), m_Storages(view.m_Storages)
{
#ifndef NDEBUG
    m_Versions = view.m_Versions;
    m_LifetimeVersion = view.m_LifetimeVersion;
#endif
    SkipInvalidEntities();
}

template<class... Components>
void ComponentView<Components...>::Iterator::SkipInvalidEntities()
{
    Validate();
    if (m_BaseStorage == nullptr) {
        return;
    }

    while (m_Index < m_BaseStorage->Size())
    {
        const EntityIndex entityIndex = m_BaseStorage->GetEntityIndex(m_Index);
        if (m_Statistics != nullptr)
        {
            ++m_Statistics->Candidates;
            ++m_Statistics->AliveChecks;
        }

        const EntityGeneration generation = m_Scene->GetEntityGeneration(entityIndex);

        const bool isAlive = m_Scene->IsEntityAlive(entityIndex, generation);

        /*例えば、

            View<TransformComponent, MeshRendererComponent, TagComponent>()

            なら、意味上は次の条件です。候補Storageの所属は既に成立するため省略し、
            残りはcache済みStorageへ直接問い合わせます（Sceneの型検索は繰り返しません）。

            HasComponent<TransformComponent>(entityID)
            &&
            HasComponent<MeshRendererComponent>(entityID)
            &&
            HasComponent<TagComponent>(entityID)
        */

        if (isAlive
            &&
            (
                std::apply([&](auto*... storage) { return (Matches(storage, entityIndex) && ...); }, m_Storages)
                )
            )
        {
            if (m_Statistics != nullptr)
            {
                ++m_Statistics->Matches;
            }
            break;
        }

        ++m_Index;
    }

}

template<class... Components>
template<class T>
bool ComponentView<Components...>::Iterator::Matches(ComponentStorage<T>* storage, EntityIndex index) const
{
    // 候補Storage自身の所属はDense列から取得した時点で成立します。同じ照合を繰り返しません。
    if (static_cast<const IComponentStorage*>(storage) == m_BaseStorage)
    {
        return true;
    }
    if (m_Statistics != nullptr)
    {
        ++m_Statistics->FilterChecks;
    }
    return storage->Has(index);
}

template<class... Components>
template<class T>
T& ComponentView<Components...>::Iterator::Read(ComponentStorage<T>* storage, EntityIndex index) const
{
    if (m_Statistics != nullptr)
    {
        ++m_Statistics->ComponentReads;
    }
    if (static_cast<const IComponentStorage*>(storage) == m_BaseStorage)
    {
        return storage->GetByIndex(m_Index);
    }
    if (m_Statistics != nullptr)
    {
        ++m_Statistics->ReadChecks;
    }
    // 非候補StorageのSparse index照合は安全なGetへ残します。Component参照は保存しません。
    return storage->Get(index);
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
    return m_Scene == other.m_Scene && m_BaseStorage == other.m_BaseStorage && m_Index == other.m_Index;
}

template<class... Components>
bool ComponentView<Components...>::Iterator::operator!=(const Iterator& other) const
{
    return (*this == other) == false;
}

// Componentへの参照を含むタプルを返します。
// std::tuple<Entity, TransformComponent&, MeshRendererComponent&>
template<class... Components>
auto ComponentView<Components...>::Iterator::operator*() const
{
    Validate();
    if (m_BaseStorage == nullptr)
    {
        throw std::out_of_range("Cannot dereference an empty View.");
    }
    const EntityIndex entityIndex = m_BaseStorage->GetEntityIndex(m_Index);

    const EntityHandle handle{entityIndex, m_Scene->GetEntityGeneration(entityIndex)};
    return std::apply([&](auto*... storage)
    {
        return std::tuple<Entity, Components&...>(Entity(handle, m_Scene), Read(storage, entityIndex)...);
    }, m_Storages);
}

template<class... Components>
typename ComponentView<Components...>::Iterator
ComponentView<Components...>::begin()
{
    Validate();
    return Iterator(*this, 0);
}

template<class... Components>
typename ComponentView<Components...>::Iterator
ComponentView<Components...>::begin() const
{
    Validate();
    return Iterator(*this, 0);
}

template<class... Components>
typename ComponentView<Components...>::Iterator
ComponentView<Components...>::end() const
{

    Validate();
    const std::size_t endIndex = (m_BaseStorage != nullptr) ? m_BaseStorage->Size() : 0;
    return Iterator(*this, endIndex);
}

template<class... Components>
typename ComponentView<Components...>::Iterator
ComponentView<Components...>::end()
{
    Validate();
    const std::size_t endIndex = (m_BaseStorage != nullptr) ? m_BaseStorage->Size() : 0;
    return Iterator(*this, endIndex);
}

template<class... Components>
bool ComponentView<Components...>::Empty() const
{
    return begin() == end();
}

template<class... Components>
std::size_t
ComponentView<Components...>::CandidateCount() const
{
    Validate();
    if (m_BaseStorage == nullptr) {
        return 0;
    }

    return m_BaseStorage->Size();
}

template<class... Components>
std::size_t ComponentView<Components...>::Size() const
{
    std::size_t count = 0;
    for (auto iterator = begin(); iterator != end(); ++iterator)
    {
        ++count;
    }
    return count;
}

}
