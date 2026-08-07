#include "Raven/Scene/Scene.h"
#include "Raven/Core/Event.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"

namespace Raven
{

Scene::Scene()
{
    m_EntitySlots.push_back(EntitySlot{});
}

bool Scene::IsEntityAlive(EntityIndex index, EntityGeneration generation) const
{
    if (index == InvalidEntityIndex) {
        return false;
    }

    if (static_cast<std::size_t>(index) >= m_EntitySlots.size())
    {
        return false;
    }

    const EntitySlot& slot = m_EntitySlots[index];

    return (slot.Alive && slot.Generation == generation);

}

bool Scene::IsEntityAlive(EntityHandle handle) const
{
	return IsEntityAlive(handle.m_Index, handle.m_Generation);
}

bool Scene::IsEntityAlive(Entity entity) const
{
    return IsEntityAlive(entity.GetIndex(),entity.GetGeneration());
}

Entity Scene::CreateEntity(const std::string& name)
{
    // 新しいEntityを作るときは、まずFree Listを確認します。
    EntityIndex index = InvalidEntityIndex;

    if (m_FreeEntityIndices.empty() == false)
    {
        index = m_FreeEntityIndices.back();
        m_FreeEntityIndices.pop_back();
    }
    else
    {
        index = static_cast<EntityIndex>(m_EntitySlots.size());

        m_EntitySlots.push_back(EntitySlot{});
    }

    EntitySlot& slot = m_EntitySlots[index];

    slot.Alive = true;

    const EntityHandle handle{index, slot.Generation};

    Entity entity(handle, this);

    AddComponent<TagComponent>(index, TagComponent{ name });

    AddComponent<TransformComponent>(index);

    return entity;

#if 0
#if USE_STORAGE_VERSION_2
    const EntityID id = m_NextEntityID++;

    AddComponent<TagComponent>(id, TagComponent{ name });

    AddComponent<TransformComponent>(id);

    return Entity(id, this);

#elif USE_STORAGE_VERSION_1
    const EntityID id = m_NextEntityID++;

    m_Tags.Emplace(id, TagComponent{ name });
    m_Transforms.Emplace(id);

    return Entity(id, this);
#else
    EntityID id = m_NextEntityID++;

    m_Tags[id] = TagComponent{ name };
    m_Transforms[id] = TransformComponent{};

    return Entity(id, this);
#endif
#endif
}

void Scene::DestroyEntity(Entity entity)
{
    if (IsEntityAlive(entity) == false)
        return;

    const EntityIndex index = entity.GetIndex();

    for (auto& entry : m_ComponentStorages)
    {
        entry.second->Remove(index);
    }

    EntitySlot& slot = m_EntitySlots[index];

    slot.Alive = false;

    // 重要なのはこの部分です。
    // 破棄時にGenerationを増やします。
	// Generationを増やすことで、古いEntityと新しいEntityを区別できるようにします。

    /*古いEntityは、
    Index = 5,Generation = 0
    のままです。

    次に同じIndexが再利用されると、
    Index = 5, Generation = 1
    になります。

    そのため、古いEntityは無効です。
    */
    ++slot.Generation;

    m_FreeEntityIndices.push_back(index);

#if 0
#if USE_STORAGE_VERSION_2
    const EntityID id = entity.GetID();

    for (auto& [type, storage] : m_ComponentStorages)
    {
        storage->Remove(id);
    }
#elif USE_STORAGE_VERSION_1
    const EntityID id = entity.GetID();

    m_Tags.Remove(id);
    m_Transforms.Remove(id);
    m_MeshRenderers.Remove(id);
#else
    EntityID id = entity.GetID();

    m_Tags.erase(id);
    m_Transforms.erase(id);
    m_MeshRenderers.erase(id);
#endif
#endif
}

void Scene::OnCreate()
{
}

void Scene::OnDestroy()
{
}

void Scene::OnUpdate(float dt)
{
    OnUpdateGame(dt);
    OnUpdatePhysics(dt);
    OnUpdateLayer(dt);
    FlushDestroyedEntities();

}

void Scene::OnUpdatePhysics(float dt)
{
    m_PhysicsAccumulator += dt;

    while (m_PhysicsAccumulator >= m_FixedDeltaTime)
    {
        m_PhysicsWorld.Step(*this, m_FixedDeltaTime);
        m_PhysicsAccumulator -= m_FixedDeltaTime;
    }

    // PhysicsDebugRendererには別Worldを再構築させず、このSceneが実際にStepした
    // PhysicsWorldを読み取り専用で関連付けます。
    ph::PhysicsDebugRenderer::BindPhysicsWorld(*this, m_PhysicsWorld);
}

void Scene::OnUpdateLayer(float dt)
{
	for (auto& layer : m_layers)
	{
		layer->OnUpdate(dt);
	}
}

void Scene::OnRender()
{
    for (auto& layer : m_layers) {
        layer->OnRender();
    }
}

void Scene::RenderEntities()
{
#if USE_STORAGE_VERSION_2

	// TransformComponentとMeshRendererComponentを持つエンティティを取得して描画する
    for (auto [entity, transform, meshRenderer] : View<TransformComponent, MeshRendererComponent>())
    {
        //static_cast<void>(entity);

        if (meshRenderer.IsValid() == false) {
            continue;
        }

        Renderer::Draw(meshRenderer.Mesh, meshRenderer.Material, transform.GetTransform());
    }

#if 0
    const auto* meshStorage = FindStorage<MeshRendererComponent>();

    if (meshStorage == nullptr) {
        return;
    }

    for (const auto& [id, meshRenderer] : *meshStorage)
    {
        if (meshRenderer.IsValid() == false) {
            continue;
        }

        const TransformComponent* transform = TryGetComponent<TransformComponent>(id);

        if (transform == nullptr) {
            continue;
        }

        Renderer::Draw(meshRenderer.Mesh, meshRenderer.Material, transform->GetTransform());
    }
#endif

#else
    for (const auto& [id, meshRenderer] : m_MeshRenderers)
    {
        if (meshRenderer.IsValid() == false) {
            continue;
        }

        if (HasComponent<TransformComponent>(id) == false) {
            continue;
        }

        const auto& transform = GetComponent<TransformComponent>(id);

        Renderer::Draw(meshRenderer.Mesh, meshRenderer.Material, transform.GetTransform());

    }
#endif
}

void Scene::OnEvent(Event& e)
{
    for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it)
    {
        (*it)->OnEvent(e);
        if (e.Handled) {
            break;
        }
    }
}

void Scene::PushLayer(Scope<Layer> layer)
{
    layer->OnAttach();
    m_layers.push_back(std::move(layer));
}

void Scene::QueueDestroyEntity(Entity entity)
{
    if (IsEntityAlive(entity) == false) {
        return;
    }

    m_DestroyQueue.push_back(entity);

#if 0
    // 同じEntityを複数回予約する可能性があるなら
    if (!entity) {
        return;
    }

    const EntityID entityID = entity.GetID();

    const auto iterator = std::find(m_DestroyQueue.begin(), m_DestroyQueue.end(), entityID);

    if (iterator == m_DestroyQueue.end())
    {
        m_DestroyQueue.push_back(entityID);
    }
}
#endif
}

void Scene::FlushDestroyedEntities()
{
    /*for (EntityIndex entityID : m_DestroyQueue)
    {
        for (auto& entry : m_ComponentStorages)
        {
            entry.second->Remove(entityID);
        }
    }

    m_DestroyQueue.clear();*/
    for (Entity entity : m_DestroyQueue)
    {
        DestroyEntity(entity);
    }

    m_DestroyQueue.clear();
}

EntityGeneration Scene::GetEntityGeneration(EntityIndex index) const
{
    if (index == InvalidEntityIndex
        || static_cast<std::size_t>(index) >= m_EntitySlots.size()
    ) {
        throw std::out_of_range("Invalid entity index.");
    }

    return m_EntitySlots[index].Generation;
}

#if 0

void TestEntityHandle()
{
    const EntityHandle first{10, 3};

    const uint64_t packed = first.Value();

    const EntityHandle restored = EntityHandle::FromValue(packed);

    assert(first == restored);
    assert(first.Index == 10);
    assert(first.Generation == 3);

    const EntityHandle second{10, 4};

    assert(first != second);

    std::unordered_set<EntityHandle> handles;

    handles.insert(first);
    handles.insert(second);

    assert(handles.size() == 2);
    assert(handles.contains(first));
    assert(handles.contains(second));
}

void TestEntityrecycle()
{
    Entity oldEntity = scene.CreateEntity("Old");

    const EntityHandle oldHandle = oldEntity.GetHandle();

    scene.DestroyEntity(oldEntity);

    assert(!oldEntity);

    Entity newEntity = scene.CreateEntity("New");

    const EntityHandle newHandle = newEntity.GetHandle();

    assert(oldHandle.Index == newHandle.Index);

    assert(oldHandle.Generation != newHandle.Generation);

    assert(oldHandle != newHandle);
    assert(oldEntity != newEntity);
}

void TestEntityGeneration()
{
    Entity first = scene.CreateEntity("First");

    const EntityIndex reusedIndex = first.GetIndex();

    const EntityGeneration oldGeneration = first.GetGeneration();

    scene.DestroyEntity(first);

    assert(!first);

    Entity second = scene.CreateEntity("Second");

    assert(second.GetIndex() == reusedIndex);

    assert(second.GetGeneration() != oldGeneration);

    assert(first != second);
    assert(!scene.IsEntityAlive(first));
    assert(scene.IsEntityAlive(second));
}

// Sparse Set版の動作確認
void TestSparseComponentStorage()
{
    Raven::ComponentStorage<Raven::TransformComponent> storage;

    auto& first = storage.Emplace(5);

    first.Position.x = 10.0f;

    auto& second = storage.Emplace(2);

    second.Position.x = 20.0f;

    auto& third = storage.Emplace(9);

    third.Position.x = 30.0f;

    assert(storage.Size() == 3);

    assert(storage.Has(5));
    assert(storage.Has(2));
    assert(storage.Has(9));
    assert(!storage.Has(100));

    assert(storage.Get(5).Position.x == 10.0f);

    assert(storage.Get(2).Position.x == 20.0f);

    assert(storage.Get(9).Position.x == 30.0f);

    const bool removed = storage.Remove(2);

    assert(removed);
    assert(!storage.Has(2));
    assert(storage.Size() == 2);

    /*
     * swap-and-pop後も、
     * 移動したEntity 9を正しく取得できる。
     */
    assert(storage.Has(9));

    assert(storage.Get(9).Position.x == 30.0f);

    assert(!storage.Remove(2));

    storage.Clear();

    assert(storage.Empty());
    assert(!storage.Has(5));
    assert(!storage.Has(9));
}
#endif

}
