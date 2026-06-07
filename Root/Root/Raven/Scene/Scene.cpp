#include "Scene.h"

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

}