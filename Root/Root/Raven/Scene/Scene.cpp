#include "Raven/Scene/Scene.h"
#include "Raven/Core/CPUProfiler.h"
#include "Raven/Core/Event.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"
#include "Raven/Physics/Thermal/ThermalSystem.h"
#include "Raven/Animation/AnimationSystem.h"

#include <cmath>

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
    return m_PhysicsWorld.GetRigidBodyWorld();
}

ph::PhysicsWorld& Scene::GetPhysicsWorld()
{
    return m_PhysicsWorld.GetRigidBodyWorld();
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

    // ThermalWorldはECS Componentを所有しないため、Scene破棄時にはStorage解放後に参照を残さないよう
    // 非所有Registryも明示的に空にします。
    m_PhysicsWorld.GetThermalWorld().Clear();
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
    // Game LogicによるThermal Componentの追加・削除・設定変更をFixed Stepへ反映します。
    // Registryはこのframe中のFixed Step群が終わるまでECS Componentを参照するだけで、所有しません。
    ph::ThermalSystem::SynchronizeWorld(*this);

    m_PhysicsAccumulator += dt;
    uint32_t fixedStepCount = 0u;

    while (m_PhysicsAccumulator >= m_FixedDeltaTime
        && fixedStepCount < m_MaxPhysicsStepsPerFrame)
    {
        // Fixed timestepが1 Application frame中に複数回走った場合、Physics Stateだけを連続して進めます。
        // SoftBody Mesh/GPU同期はloop終了後へ集約し、catch-up途中Stateの不要なuploadを避けます。
        {
            RAVEN_PROFILE_SCOPE("Physics.FixedStep");
            m_PhysicsWorld.StepSimulation(*this, m_FixedDeltaTime);
        }
        m_PhysicsAccumulator -= m_FixedDeltaTime;
        ++fixedStepCount;
    }

    if (fixedStepCount > 0u)
    {
        RAVEN_PROFILE_SCOPE("Physics.OutputSynchronization");
        m_PhysicsWorld.SynchronizeOutputs();
    }

    // 遅いframeの遅れを全て次frameへ持ち越すと、catch-up自身が次の遅れを作り続けます。
    // 上限到達後は整数step分だけ破棄し、1step未満の端数は保持して通常時の時間積分を維持します。
    // Solverのdtを大きくして追いつかせないため、衝突・XPBD・Rigid/Soft反作用の順序と精度は変わりません。
    // 過負荷時はPhysicsの進行時間が実時間より短くなるため、破棄量も必ず診断へ公開します。
    float droppedTime = 0.0f;
    if (m_PhysicsAccumulator >= m_FixedDeltaTime)
    {
        const float remainder = std::fmod(m_PhysicsAccumulator, m_FixedDeltaTime);
        droppedTime = m_PhysicsAccumulator - remainder;
        m_PhysicsAccumulator = remainder;
    }

    // 0 Stepのframeも記録し、1回あたりの重さとcatch-up回数を区別できるようにします。
    CPUProfiler::Get().AddCounter("Physics.FixedStep.Count", static_cast<double>(fixedStepCount));
    CPUProfiler::Get().AddCounter("Physics.FixedStep.SimulatedMilliseconds",
        static_cast<double>(fixedStepCount) * static_cast<double>(m_FixedDeltaTime) * 1000.0);
    CPUProfiler::Get().AddCounter("Physics.FixedStep.DroppedMilliseconds", droppedTime * 1000.0);
    CPUProfiler::Get().AddCounter("Physics.FixedStep.AccumulatorMilliseconds", m_PhysicsAccumulator * 1000.0);

    // PhysicsDebugRendererには別Worldを再構築させず、このSceneが実際にStepした
    // Rigid Body PhysicsWorldを読み取り専用で関連付けます。
    ph::PhysicsDebugRenderer::BindPhysicsWorld(*this, m_PhysicsWorld.GetRigidBodyWorld());
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

    // Scene単体利用時も描画できるよう、基底Sceneは既存のEntity描画を担当します。
    // 派生Sceneが独自Renderを持つ場合はoverrideできます。
    RenderEntities();
}

void Scene::OnEvent(Event& e)
{
    for (auto& layer : m_layers)
    {
        layer->OnEvent(e);
    }
}

void Scene::PushLayer(Scope<Layer> layer)
{
    if (layer == nullptr)
    {
        return;
    }

    layer->OnAttach();
    m_layers.push_back(std::move(layer));
}

void Scene::RenderEntities()
{
    auto view = View<TransformComponent, MeshRendererComponent>();

    for (auto [entity, transform, renderer] : view)
    {
        if (renderer.IsValid() == false)
        {
            continue;
        }

        Renderer::Submit(
            renderer.Mesh,
            renderer.Material,
            transform.GetTransform());
    }
}

void Scene::QueueDestroyEntity(Entity entity)
{
    if (IsEntityAlive(entity) == false)
    {
        return;
    }

    const auto it = std::find_if(
        m_DestroyQueue.begin(),
        m_DestroyQueue.end(),
        [&entity](const Entity& queued)
        {
            return queued.GetHandle() == entity.GetHandle();
        });

    if (it == m_DestroyQueue.end())
    {
        m_DestroyQueue.push_back(entity);
    }
}

void Scene::FlushDestroyedEntities()
{
    for (const Entity& entity : m_DestroyQueue)
    {
        DestroyEntity(entity);
    }

    m_DestroyQueue.clear();
}

}