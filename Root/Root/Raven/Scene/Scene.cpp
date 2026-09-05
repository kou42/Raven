#include "Raven/Scene/Scene.h"
#include "Raven/Core/CPUProfiler.h"
#include "Raven/Core/Event.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"
#include "Raven/Animation/AnimationSystem.h"

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

const ph::PhysicsWorld& Scene::GetPhysicsWorld() const
{
    return m_PhysicsWorld;
}

ph::PhysicsWorld& Scene::GetPhysicsWorld()
{
    return m_PhysicsWorld;
}

void Scene::OnCreate()
{
}

void Scene::OnDestroy()
{
    // ========================================================================
    // Scene Layer shutdown
    // ========================================================================
    // Scene::PushLayer()は登録時にOnAttach()を呼ぶため、終了時には必ず対応するOnDetach()を呼びます。
    // 後から積まれたLayerほど先に終了するLIFO順にすることで、Overlay等が先に積まれたLayerを
    // 参照している場合でも依存先より先に破棄できます。
    //
    // LayerがScene Entityを生成している場合があるため、Entity最終Sweepより前にDetachすることが重要です。
    // これにより通常の所有者責務でEntityを解放した後、本当に取りこぼされたEntityだけを下のSweepが回収します。
    for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it)
    {
        if (*it != nullptr)
        {
            (*it)->OnDetach();
        }
    }
    m_layers.clear();

    // ========================================================================
    // Scene final entity sweep
    // ========================================================================
    // 通常はEntityを生成したSceneGame / Layer / Spawner自身が明示的にDestroyEntity()を呼びます。
    // ここはその所有責務を置き換えるものではなく、Scene終了時に取りこぼされたEntityを残さないための
    // 最終安全網です。
    //
    // EntitySlotを正規データとして走査するため、特定Componentや描画対象Listには依存しません。
    // MeshRendererを持たないCamera / Physics Entityや、将来追加されるGameplay Entityも同じ規則で
    // 確実に破棄できます。
    //
    // Destroy Queueに同じEntityが残っていても、先にQueueを捨てて現在AliveなGenerationだけを
    // 直接破棄するため、終了処理後に古いHandleを再処理することはありません。
    m_DestroyQueue.clear();

    for (EntityIndex index = 1u;
         index < static_cast<EntityIndex>(m_EntitySlots.size());
         ++index)
    {
        const EntitySlot& slot = m_EntitySlots[index];
        if (slot.Alive == false)
        {
            continue;
        }

        const EntityHandle handle{ index, slot.Generation };
        DestroyEntity(Entity(handle, this));
    }

    // DestroyEntity()ですべてのComponentはRemove済みですが、Storage自体が保持するAllocatorや
    // shared_ptr等の内部容量もScene終了時に解放するためContainerも破棄します。
    m_ComponentStorages.clear();

    m_PhysicsAccumulator = 0.0f;
}

void Scene::OnUpdate(float dt)
{
    RAVEN_PROFILE_SCOPE("Scene.Update");

    // Scene全体だけでなく主要System単位でもScopeを分けます。
    // 後でJob System化する際に「どのSystemを先に並列化すべきか」をFrame単位で判断できます。
    {
        RAVEN_PROFILE_SCOPE("Scene.GameLogic");
        OnUpdateGame(dt);
    }

    // Game LogicがPlay/Pause/Clip切り替えなどを行った後にAnimationを評価します。
    // AnimationSystemがTransformへPoseを反映してからPhysicsへ進むことで、
    // Kinematic Bodyなどは更新済みTransformをPhysics側から参照できます。
    {
        RAVEN_PROFILE_SCOPE("Scene.Animation");
        AnimationSystem::Update(*this, dt);
    }

    {
        RAVEN_PROFILE_SCOPE("Scene.Physics");
        OnUpdatePhysics(dt);
    }

    {
        RAVEN_PROFILE_SCOPE("Scene.Layers");
        OnUpdateLayer(dt);
    }

    {
        RAVEN_PROFILE_SCOPE("Scene.DestroyQueue");
        FlushDestroyedEntities();
    }
}

void Scene::OnUpdatePhysics(float dt)
{
    m_PhysicsAccumulator += dt;
    uint32_t fixedStepCount = 0u;

    while (m_PhysicsAccumulator >= m_FixedDeltaTime)
    {
        // Fixed timestepが1 Application frame中に複数回走った場合も1回ずつ記録します。
        // Statistics側で同名Scopeを集計することで、Physics catch-upによる負荷増加も確認できます。
        {
            RAVEN_PROFILE_SCOPE("Physics.FixedStep");
            m_PhysicsWorld.Step(*this, m_FixedDeltaTime);
        }
        m_PhysicsAccumulator -= m_FixedDeltaTime;
        ++fixedStepCount;
    }

    // 0 Stepのframeも記録し、1回あたりの重さとcatch-up回数を区別できるようにします。
    // 残時間は次frameへ持ち越す元の契約を維持し、時間の破棄やStep数制限は行いません。
    CPUProfiler::Get().AddCounter("Physics.FixedStep.Count", static_cast<double>(fixedStepCount));
    CPUProfiler::Get().AddCounter("Physics.FixedStep.SimulatedMilliseconds",
        static_cast<double>(fixedStepCount) * static_cast<double>(m_FixedDeltaTime) * 1000.0);

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
    RAVEN_PROFILE_SCOPE("Scene.Render");

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
