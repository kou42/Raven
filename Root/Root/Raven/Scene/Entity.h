#pragma once

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

// Entity = ID
// Component = データ
// System = 処理

// using EntityID = uint32_t
// でエンティティを定義し、
// TransformComponent, MeshRendererComponent, TagComponent などのコンポーネントを定義する。
// RigitBodyComponent, ColliderComponent, LightComponent CameraCompomentなどのコンポーネントも追加可能。

namespace Raven
{

class Scene;

using EntityID = uint32_t;

class Entity
{
public:
    Entity() = default;
    Entity(EntityID id, Scene* scene)
        : m_ID(id), m_Scene(scene) {}

    template <class T, class... Args>
    T& AddComponent(Args&&... args);

    template <class T>
    T& GetComponent();

    template <class T>
    const T& GetComponent() const;

    template <class T>
    bool HasComponent() const;

    template <class T>
    void RemoveComponent();

    EntityID GetID() const
    {
        return m_ID;
    }

    explicit operator bool() const
    {
        return m_ID != 0 && m_Scene != nullptr;
    }

    bool operator==(const Entity& other) const
    {
        return m_ID == other.m_ID && m_Scene == other.m_Scene;
    }

    bool operator!=(const Entity& other) const
    {
        return !(*this == other);
    }

private:
    EntityID m_ID = 0;
    Scene* m_Scene = nullptr;
};

}