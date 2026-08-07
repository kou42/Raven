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

    // Generationを進めることで、同じIndexが再利用されても古いEntity Handleを無効化します。
    ++slot.Generation;
    m_FreeEntityIndices.push_back(index);
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

    // Debug Rendererには別のPhysicsWorldを作らせず、このSceneが実際にStepしている
    // PhysicsWorldを読み取り専用で関連付けます。
    // これによりFat AABB / Dynamic Tree / Contact Point / Contact Normalは、
    // Solverへ渡されたSimulationと同じデータを可視化できます。
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
    for (auto [entity, transform, meshRenderer] : View<TransformComponent, MeshRendererComponent>())
    {
        if (meshRenderer.IsValid() == false) {
            continue;
        }

        Renderer::Draw(meshRenderer.Mesh, meshRenderer.Material, transform.GetTransform());
    }
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
}

void Scene::FlushDestroyedEntities()
{
    for (Entity entity : m_DestroyQueue)
    {
        DestroyEntity(entity);
    }

    m_DestroyQueue.clear();
}

EntityGeneration Scene::GetEntityGeneration(EntityIndex index) const
{
    if (index == InvalidEntityIndex
        || static_cast<std::size_t>(index) >= m_EntitySlots.size())
    {
        throw std::out_of_range("Invalid entity index.");
    }

    return m_EntitySlots[index].Generation;
}

}
