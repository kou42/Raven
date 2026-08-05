#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{

Entity::Entity(EntityIndex index, EntityGeneration generation, Scene* scene)
{
	EntityHandle handle{ index, generation };
    Entity(handle, scene);
}

Entity::Entity(EntityHandle handle, Scene* scene)
{
	m_Handle = handle;
    m_Scene = scene;
}

EntityHandle Entity::GetHandle() const
{
    return m_Handle;
}

uint64_t Entity::GetValue() const
{
    return m_Handle.Value();
}

EntityIndex Entity::GetIndex() const
{
    return m_Handle.m_Index;
}

EntityGeneration Entity::GetGeneration() const
{
    return m_Handle.m_Generation;
}

Scene* Entity::GetScene() const noexcept
{
    return m_Scene;
}

Entity::operator bool() const
{
    if (m_Scene == nullptr) {
        return false;
    }

    return m_Scene->IsEntityAlive(GetIndex(), GetGeneration());
}

}
