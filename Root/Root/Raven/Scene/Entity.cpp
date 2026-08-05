#include "Raven/Scene/Entity.h"

namespace Raven
{

Entity::Entity(EntityIndex index, EntityGeneration generation, Scene* scene)
{
    m_Index = index;
    m_Generation = generation;
    m_Scene = scene;
}

EntityIndex Entity::GetIndex() const
{
    return m_Index;
}

EntityGeneration Entity::GetGeneration() const
{
    return m_Generation;
}

}
