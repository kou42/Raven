#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

//#include "Raven/Core/Event.h"
#include "Raven/Core/Base.h"
#include "Raven/Renderer/Layer/Layer.h"

#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Components.h"

namespace Raven
{
class Scene
{

public:
    Scene() = default;
    ~Scene() = default;

    //+---------------------------------------------------------------------
    // 仮想関数
    //+---------------------------------------------------------------------
    virtual void OnCreate();
    virtual void OnDestroy();
    virtual void OnUpdate(float dt);
    virtual void OnRender();
    virtual void OnEvent(Event& e);

    //+---------------------------------------------------------------------
    // メンバ関数
    //+---------------------------------------------------------------------
    void PushLayer(Scope<Layer> layer);

    Entity CreateEntity(const std::string& name = "Entity");
    void DestroyEntity(Entity entity);

    template <class T, class... Args>
    T& AddComponent(EntityID id, Args&&... args);

    template <class T>
    T& GetComponent(EntityID id);

    template <class T>
    const T& GetComponent(EntityID id) const;

    template <class T>
    bool HasComponent(EntityID id) const;

    template <class T>
    void RemoveComponent(EntityID id);

    const std::unordered_map<EntityID, TagComponent>& GetTags() const
    {
        return m_Tags;
    }

    const std::unordered_map<EntityID, TransformComponent>& GetTransforms() const
    {
        return m_Transforms;
    }

    const std::unordered_map<EntityID, MeshRendererComponent>& GetMeshRenderers() const
    {
        return m_MeshRenderers;
    }

protected:
    std::vector<Scope<Layer>> m_layers;

private:
    EntityID m_NextEntityID = 1;

    std::unordered_map<EntityID, TagComponent> m_Tags;
    std::unordered_map<EntityID, TransformComponent> m_Transforms;
    std::unordered_map<EntityID, MeshRendererComponent> m_MeshRenderers;
};

template <class T, class... Args>
T& Scene::AddComponent(EntityID id, Args&&... args)
{
    if constexpr (std::is_same_v<T, TagComponent>)
    {
        return m_Tags.emplace(id, T{ std::forward<Args>(args)... }).first->second;
    }
    else if constexpr (std::is_same_v<T, TransformComponent>)
    {
        return m_Transforms.emplace(id, T{ std::forward<Args>(args)... }).first->second;
    }
    else if constexpr (std::is_same_v<T, MeshRendererComponent>)
    {
        return m_MeshRenderers.emplace(id, T{ std::forward<Args>(args)... }).first->second;
    }
}

template <class T>
T& Scene::GetComponent(EntityID id)
{
    if constexpr (std::is_same_v<T, TagComponent>)
        return m_Tags.at(id);
    else if constexpr (std::is_same_v<T, TransformComponent>)
        return m_Transforms.at(id);
    else if constexpr (std::is_same_v<T, MeshRendererComponent>)
        return m_MeshRenderers.at(id);
}

template <class T>
const T& Scene::GetComponent(EntityID id) const
{
    if constexpr (std::is_same_v<T, TagComponent>)
        return m_Tags.at(id);
    else if constexpr (std::is_same_v<T, TransformComponent>)
        return m_Transforms.at(id);
    else if constexpr (std::is_same_v<T, MeshRendererComponent>)
        return m_MeshRenderers.at(id);
}

template <class T>
bool Scene::HasComponent(EntityID id) const
{
    if constexpr (std::is_same_v<T, TagComponent>)
        return m_Tags.find(id) != m_Tags.end();
    else if constexpr (std::is_same_v<T, TransformComponent>)
        return m_Transforms.find(id) != m_Transforms.end();
    else if constexpr (std::is_same_v<T, MeshRendererComponent>)
        return m_MeshRenderers.find(id) != m_MeshRenderers.end();
}

template <class T>
void Scene::RemoveComponent(EntityID id)
{
    if constexpr (std::is_same_v<T, TagComponent>)
        m_Tags.erase(id);
    else if constexpr (std::is_same_v<T, TransformComponent>)
        m_Transforms.erase(id);
    else if constexpr (std::is_same_v<T, MeshRendererComponent>)
        m_MeshRenderers.erase(id);
}


template <class T, class... Args>
T& Entity::AddComponent(Args&&... args)
{
    return m_Scene->AddComponent<T>(m_ID, std::forward<Args>(args)...);
}

template <class T>
T& Entity::GetComponent()
{
    return m_Scene->GetComponent<T>(m_ID);
}

template <class T>
const T& Entity::GetComponent() const
{
    return m_Scene->GetComponent<T>(m_ID);
}

template <class T>
bool Entity::HasComponent() const
{
    return m_Scene->HasComponent<T>(m_ID);
}

template <class T>
void Entity::RemoveComponent()
{
    m_Scene->RemoveComponent<T>(m_ID);
}

}