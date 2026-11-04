#pragma once

namespace Raven
{

template<class T>
std::optional<T> DeleteEntityCommand::Capture(const Entity& entity)
{
    if (entity.HasComponent<T>() == false)
    {
        return std::nullopt;
    }
    return entity.GetComponent<T>();
}

template<class T>
void DeleteEntityCommand::Restore(Entity& entity, const std::optional<T>& value)
{
    if (value.has_value() == false)
    {
        return;
    }
    if (entity.HasComponent<T>() == true)
    {
        entity.GetComponent<T>() = *value;
    }
    else
    {
        entity.AddComponent<T>(*value);
    }
}


}
