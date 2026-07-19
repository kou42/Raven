#include "Raven/Scene/Scene.h"
#include "Raven/Core/Event.h"

namespace Raven
{

Entity Scene::CreateEntity(const std::string& name)
{
    EntityID id = m_NextEntityID++;

    m_Tags[id] = TagComponent{ name };
    m_Transforms[id] = TransformComponent{};

    return Entity(id, this);
}

void Scene::DestroyEntity(Entity entity)
{
    EntityID id = entity.GetID();

    m_Tags.erase(id);
    m_Transforms.erase(id);
    m_MeshRenderers.erase(id);
}

void Scene::OnCreate()
{
}

void Scene::OnDestroy()
{
}

void Scene::OnUpdate(float dt)
{
    for (auto& layer : m_layers) {
        layer->OnUpdate(dt);
    }
}

void Scene::OnRender()
{
    for (auto& layer : m_layers) {
        layer->OnRender();
    }
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

}